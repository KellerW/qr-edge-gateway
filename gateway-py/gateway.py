import json
import os
import time
import logging
from typing import Any, Dict, Optional

import requests
import paho.mqtt.client as mqtt


def env_str(name: str, default: str) -> str:
    v = os.getenv(name)
    return v if v else default


def env_int(name: str, default: int) -> int:
    v = os.getenv(name)
    try:
        return int(v) if v else default
    except ValueError:
        return default


MQTT_HOST = env_str("MQTT_HOST", "test.mosquitto.org")
MQTT_PORT = env_int("MQTT_PORT", 8883)
MQTT_CERT = env_str("MQTT_CERT", "/tmp/mosquitto.org.crt")
TOPIC_CMD = env_str("MQTT_TOPIC_CMD", "from_cloud/commands")
TOPIC_EVT = env_str("MQTT_TOPIC_EVENT", "from_device/events")

QR_API_BASE_URL = env_str("QR_API_BASE_URL", "http://qr-c:8080")
QR_START_TIMEOUT_MS = env_int("QR_START_TIMEOUT_MS", 1000)

LOG_PATH = env_str("LOG_PATH", "/var/log/gateway-py/gateway.log")

POLL_INTERVAL_SEC = float(env_str("POLL_INTERVAL_SEC", "0.3"))
POLL_MAX_SECONDS = float(env_str("POLL_MAX_SECONDS", "6.0"))


def setup_logger() -> logging.Logger:
    logger = logging.getLogger("gateway-py")
    logger.setLevel(logging.INFO)

    fmt = logging.Formatter("%(asctime)s %(levelname)s %(message)s")

    sh = logging.StreamHandler()
    sh.setFormatter(fmt)
    logger.addHandler(sh)

    try:
        os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
        fh = logging.FileHandler(LOG_PATH)
        fh.setFormatter(fmt)
        logger.addHandler(fh)
    except Exception:
        # If file logging fails, console is still fine.
        pass

    return logger


log = setup_logger()


def publish_event(client: mqtt.Client, payload: Dict[str, Any]) -> None:
    msg = json.dumps(payload, separators=(",", ":"), ensure_ascii=False)
    client.publish(TOPIC_EVT, msg, qos=1, retain=False)
    log.info("MQTT publish -> %s: %s", TOPIC_EVT, msg)


def http_post(path: str, body: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    url = f"{QR_API_BASE_URL}{path}"
    r = requests.post(url, json=body, timeout=5)
    return {"status_code": r.status_code, "json": r.json() if r.content else {}}


def http_get(path: str) -> Dict[str, Any]:
    url = f"{QR_API_BASE_URL}{path}"
    r = requests.get(url, timeout=5)
    return {"status_code": r.status_code, "json": r.json() if r.content else {}}


def handle_command(client: mqtt.Client, cmd: str) -> None:
    # Maps to qr-c POST /command
    res = http_post("/command", {"command": cmd})
    payload = res["json"]
    payload.setdefault("type", "command_ack")
    publish_event(client, payload)


def handle_start(client: mqtt.Client, timeout_ms: int) -> None:
    # Calls qr-c POST /start then polls /result/{jobId}
    res = http_post("/start", {"timeout_ms": timeout_ms})
    j = res["json"]

    if res["status_code"] != 202:
        # start rejected
        publish_event(client, {"type": "job_rejected", "ok": False, "message": j.get("message", "ERR")})
        return

    job_id = j.get("jobId")
    publish_event(client, {"type": "job_accepted", "jobId": job_id, "state": j.get("state"), "message": j.get("message")})

    deadline = time.time() + POLL_MAX_SECONDS
    while time.time() < deadline:
        gr = http_get(f"/result/{job_id}")
        gj = gr["json"]
        if gr["status_code"] == 200:
            status = gj.get("status")
            if status in ("DONE", "TIMEOUT"):
                out = {
                    "type": "job_result",
                    "jobId": job_id,
                    "status": status,
                    "message": gj.get("message"),
                    "state": gj.get("state"),
                }
                data = gj.get("data") or {}
                if "qr" in data:
                    out["qr"] = data["qr"]
                publish_event(client, out)
                return
        time.sleep(POLL_INTERVAL_SEC)

    publish_event(client, {"type": "job_result", "jobId": job_id, "status": "TIMEOUT", "message": "ERR:POLL_TIMEOUT"})


def on_connect(client: mqtt.Client, userdata, flags, reason_code, properties=None):
    log.info("MQTT connected rc=%s", reason_code)
    client.subscribe(TOPIC_CMD, qos=1)
    log.info("MQTT subscribed to %s", TOPIC_CMD)


def on_message(client: mqtt.Client, userdata, msg: mqtt.MQTTMessage):
    try:
        payload = msg.payload.decode("utf-8", errors="replace")
        log.info("MQTT recv <- %s: %s", msg.topic, payload)

        data = json.loads(payload) if payload else {}
        msg_type = data.get("type", "command")

        if msg_type == "command":
            cmd = str(data.get("command", "")).strip()
            if not cmd:
                publish_event(client, {"type": "error", "message": "ERR:BAD_REQUEST"})
                return

            # If cloud sends START as command, treat it as async start.
            if cmd.upper() == "START":
                handle_start(client, int(data.get("timeout_ms", QR_START_TIMEOUT_MS)))
            else:
                handle_command(client, cmd)

        elif msg_type == "start":
            timeout_ms = int(data.get("timeout_ms", QR_START_TIMEOUT_MS))
            handle_start(client, timeout_ms)

        else:
            publish_event(client, {"type": "error", "message": "ERR:UNKNOWN_MESSAGE_TYPE"})

    except Exception as e:
        log.exception("on_message error: %s", e)
        publish_event(client, {"type": "error", "message": "ERR:INTERNAL"})


def main() -> None:
    log.info("gateway-py starting")
    log.info("MQTT_HOST=%s MQTT_PORT=%s TOPIC_CMD=%s TOPIC_EVT=%s", MQTT_HOST, MQTT_PORT, TOPIC_CMD, TOPIC_EVT)
    log.info("QR_API_BASE_URL=%s", QR_API_BASE_URL)

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

    # TLS
    client.tls_set(ca_certs=MQTT_CERT)
    client.tls_insecure_set(False)

    client.on_connect = on_connect
    client.on_message = on_message

    # Auto-reconnect with backoff
    client.reconnect_delay_set(min_delay=1, max_delay=30)

    client.connect(MQTT_HOST, MQTT_PORT, keepalive=30)
    client.loop_forever()


if __name__ == "__main__":
    main()
