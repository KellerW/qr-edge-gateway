# SPDX-License-Identifier: CC-BY-NC-4.0
import json
import os
import time
import logging
import threading
from typing import Any, Dict, Optional, Tuple

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


def env_float(name: str, default: float) -> float:
    v = os.getenv(name)
    try:
        return float(v) if v else default
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

POLL_INTERVAL_SEC = env_float("POLL_INTERVAL_SEC", 0.3)
POLL_MAX_SECONDS = env_float("POLL_MAX_SECONDS", 6.0)

HTTP_TIMEOUT_SEC = env_float("HTTP_TIMEOUT_SEC", 5.0)

# Keep aligned with qr-c validation / OpenAPI
BAUD_MIN = 1
BAUD_MAX = 2_000_000


def setup_logger() -> logging.Logger:
    logger = logging.getLogger("gateway-py")
    logger.setLevel(logging.INFO)

    fmt = logging.Formatter("%(asctime)s %(levelname)s %(message)s")

    sh = logging.StreamHandler()
    sh.setFormatter(fmt)
    logger.addHandler(sh)

    # Optional file logging (works only if volume mounted)
    try:
        os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
        fh = logging.FileHandler(LOG_PATH)
        fh.setFormatter(fmt)
        logger.addHandler(fh)
    except Exception:
        pass

    return logger


log = setup_logger()
http = requests.Session()


def safe_json(resp: requests.Response) -> Dict[str, Any]:
    if not resp.content:
        return {}
    try:
        return resp.json()
    except Exception:
        return {}


def http_post(path: str, body: Optional[Dict[str, Any]] = None) -> Tuple[int, Dict[str, Any]]:
    url = f"{QR_API_BASE_URL}{path}"
    try:
        payload = body if body is not None else {}
        r = http.post(url, json=payload, timeout=HTTP_TIMEOUT_SEC)
        return r.status_code, safe_json(r)
    except Exception as e:
        log.warning("HTTP POST %s failed: %s", url, e)
        return 0, {"ok": False, "message": "ERR:HTTP"}


def http_get(path: str) -> Tuple[int, Dict[str, Any]]:
    url = f"{QR_API_BASE_URL}{path}"
    try:
        r = http.get(url, timeout=HTTP_TIMEOUT_SEC)
        return r.status_code, safe_json(r)
    except Exception as e:
        log.warning("HTTP GET %s failed: %s", url, e)
        return 0, {"ok": False, "message": "ERR:HTTP"}


def publish_event(client: mqtt.Client, payload: Dict[str, Any]) -> None:
    msg = json.dumps(payload, separators=(",", ":"), ensure_ascii=False)
    client.publish(TOPIC_EVT, msg, qos=1, retain=False)
    log.info("MQTT publish -> %s: %s", TOPIC_EVT, msg)


def normalize_cmd(cmd: str) -> str:
    return cmd.strip().upper()


def make_cmd_id(data: Dict[str, Any]) -> str:
    cid = str(data.get("id", "")).strip()
    if cid:
        return cid
    return f"cmd-{int(time.time() * 1000)}"


def validate_params(params: Optional[Dict[str, Any]]) -> Optional[str]:
    """
    Validate params according to qr-c expectations:
    - params must be object if present (already checked by caller)
    - baudrate must be integer in [1..2_000_000] if present
    - timeout_ms must be integer > 0 if present
    """
    if params is None:
        return None

    if "baudrate" in params:
        br = params.get("baudrate")
        if not isinstance(br, int):
            return "ERR:BAD_REQUEST"
        if br < BAUD_MIN or br > BAUD_MAX:
            return "ERR:BAD_REQUEST"

    if "timeout_ms" in params:
        tmo = params.get("timeout_ms")
        if not isinstance(tmo, int):
            return "ERR:BAD_REQUEST"
        if tmo <= 0:
            return "ERR:BAD_REQUEST"

    return None


def dispatch_in_thread(fn, *args, **kwargs) -> None:
    t = threading.Thread(target=fn, args=args, kwargs=kwargs, daemon=True)
    t.start()


def handle_command_sync(client: mqtt.Client, cmd_id: str, cmd: str, params: Optional[Dict[str, Any]] = None) -> None:
    # Maps to qr-c POST /command
    req: Dict[str, Any] = {"command": cmd}
    if params is not None:
        req["params"] = params

    sc, j = http_post("/command", req)

    out = {
        "id": cmd_id,
        "type": "command_ack",
        "http_status": sc,
        "ok": j.get("ok", False),
        "command": j.get("command", cmd),
        "state": j.get("state"),
        "message": j.get("message", "ERR"),
        "data": j.get("data", {}),
    }
    publish_event(client, out)


def handle_stop(client: mqtt.Client, cmd_id: str) -> None:
    # STOP must call /stop (cancel active wait + stop core)
    sc, j = http_post("/stop", {})

    out = {
        "id": cmd_id,
        "type": "stop_ack",
        "http_status": sc,
        "ok": j.get("ok", False),
        "state": j.get("state"),
        "message": j.get("message", "ERR"),
    }
    publish_event(client, out)


def handle_start_and_poll(client: mqtt.Client, cmd_id: str, timeout_ms: int) -> None:
    # Calls qr-c POST /start then polls /result/{jobId}
    sc, j = http_post("/start", {"timeout_ms": timeout_ms})

    if sc != 202:
        publish_event(client, {
            "id": cmd_id,
            "type": "job_rejected",
            "http_status": sc,
            "ok": False,
            "message": j.get("message", "ERR"),
        })
        return

    job_id = j.get("jobId", "")
    publish_event(client, {
        "id": cmd_id,
        "type": "job_accepted",
        "http_status": sc,
        "ok": True,
        "jobId": job_id,
        "state": j.get("state"),
        "message": j.get("message"),
    })

    deadline = time.time() + POLL_MAX_SECONDS
    while time.time() < deadline:
        gsc, gj = http_get(f"/result/{job_id}")
        if gsc == 200:
            status = gj.get("status")
            if status in ("DONE", "TIMEOUT", "CANCELLED"):
                out = {
                    "id": cmd_id,
                    "type": "job_result",
                    "http_status": gsc,
                    "ok": True,
                    "jobId": job_id,
                    "status": status,
                    "message": gj.get("message"),
                    "state": gj.get("state"),
                }
                data = gj.get("data") or {}
                if isinstance(data, dict) and "qr" in data:
                    out["qr"] = data["qr"]
                publish_event(client, out)
                return

        time.sleep(POLL_INTERVAL_SEC)

    publish_event(client, {
        "id": cmd_id,
        "type": "job_result",
        "ok": False,
        "jobId": job_id,
        "status": "TIMEOUT",
        "message": "ERR:POLL_TIMEOUT",
    })


def on_connect(client: mqtt.Client, userdata, flags, reason_code, properties=None):
    log.info("MQTT connected rc=%s", reason_code)
    client.subscribe(TOPIC_CMD, qos=1)
    log.info("MQTT subscribed to %s", TOPIC_CMD)


def on_message(client: mqtt.Client, userdata, msg: mqtt.MQTTMessage):
    try:
        payload = msg.payload.decode("utf-8", errors="replace")
        log.info("MQTT recv <- %s: %s", msg.topic, payload)

        data = json.loads(payload) if payload else {}
        msg_type = str(data.get("type", "command")).strip().lower()
        cmd_id = make_cmd_id(data)

        if msg_type == "command":
            cmd = normalize_cmd(str(data.get("command", "")))
            if not cmd:
                publish_event(client, {"id": cmd_id, "type": "error", "message": "ERR:BAD_REQUEST"})
                return

            params = data.get("params")
            if params is not None and not isinstance(params, dict):
                publish_event(client, {"id": cmd_id, "type": "error", "message": "ERR:BAD_REQUEST"})
                return

            err = validate_params(params)
            if err:
                publish_event(client, {"id": cmd_id, "type": "error", "message": err})
                return

            if cmd == "START":
                timeout_ms = int(data.get("timeout_ms", QR_START_TIMEOUT_MS))
                dispatch_in_thread(handle_start_and_poll, client, cmd_id, timeout_ms)
                return

            if cmd == "STOP":
                dispatch_in_thread(handle_stop, client, cmd_id)
                return

            # INIT / PING / others map to /command
            dispatch_in_thread(handle_command_sync, client, cmd_id, cmd, params)
            return

        if msg_type == "start":
            timeout_ms = int(data.get("timeout_ms", QR_START_TIMEOUT_MS))
            dispatch_in_thread(handle_start_and_poll, client, cmd_id, timeout_ms)
            return

        if msg_type == "stop":
            dispatch_in_thread(handle_stop, client, cmd_id)
            return

        publish_event(client, {"id": cmd_id, "type": "error", "message": "ERR:UNKNOWN_MESSAGE_TYPE"})

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

    client.reconnect_delay_set(min_delay=1, max_delay=30)

    client.connect(MQTT_HOST, MQTT_PORT, keepalive=30)
    client.loop_forever()


if __name__ == "__main__":
    main()
