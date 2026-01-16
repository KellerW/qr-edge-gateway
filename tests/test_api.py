import os
import time
import requests

BASE_URL = os.getenv("BASE_URL", "http://qr-c:8080")
TIMEOUT_MS = int(os.getenv("TIMEOUT_MS", "1000"))
POLL_INTERVAL = float(os.getenv("POLL_INTERVAL", "0.3"))
POLL_MAX_SECONDS = float(os.getenv("POLL_MAX_SECONDS", "6.0"))

def url(path: str) -> str:
    return f"{BASE_URL}{path}"

def post_json(path: str, payload: dict | None, expect_status: int | None):
    r = requests.post(url(path), json=payload, timeout=3)
    if expect_status is not None:
        assert r.status_code == expect_status, (r.status_code, r.text)
    return r

def get_json(path: str, expect_status: int | None):
    r = requests.get(url(path), timeout=3)
    if expect_status is not None:
        assert r.status_code == expect_status, (r.status_code, r.text)
    return r

def assert_data_is_object(resp_json: dict):
    # Enterprise-friendly: always return data as object {} (not null)
    assert "data" in resp_json, "response must contain 'data'"
    assert isinstance(resp_json["data"], dict), f"data must be object, got: {type(resp_json['data'])}"

def get_state() -> str:
    r = get_json("/status", 200)
    j = r.json()
    assert j["ok"] is True
    return j["state"]

def poll_until_finished(job_id: str):
    deadline = time.time() + POLL_MAX_SECONDS
    last = None

    while time.time() < deadline:
        r = get_json(f"/result/{job_id}", 200)
        last = r.json()

        assert last["ok"] is True
        assert last["jobId"] == job_id
        assert last["status"] in ("PENDING", "DONE", "TIMEOUT", "CANCELLED")
        assert_data_is_object(last)

        if last["status"] != "PENDING":
            return last

        time.sleep(POLL_INTERVAL)

    assert last is not None
    assert False, f"Job did not finish within {POLL_MAX_SECONDS}s; last={last}"

def test_health():
    r = get_json("/health", 200)
    j = r.json()
    assert j["ok"] is True
    assert j["message"] == "UP"

def test_status():
    state = get_state()
    assert isinstance(state, str)

def test_ping():
    r = post_json("/command", {"command": "PING"}, 200)
    j = r.json()
    assert j["ok"] is True
    assert j["command"] == "PING"
    assert j["message"] == "PONG"
    assert "state" in j
    assert_data_is_object(j)

def test_start_requires_init_or_rejects_in_other_states():
    state = get_state()
    r = post_json("/start", {"timeout_ms": TIMEOUT_MS}, None)

    if state == "NOT_INIT":
        assert r.status_code == 409, (r.status_code, r.text)
        j = r.json()
        assert j["ok"] is False
        assert j["message"] == "ERR:NOT_INIT"
    elif state == "INIT":
        assert r.status_code == 202, (r.status_code, r.text)
        j = r.json()
        assert j["ok"] is True
        assert j["message"] == "ACCEPTED"
        assert isinstance(j["jobId"], str) and len(j["jobId"]) > 0
    else:
        # RUNNING or STOPPED should be rejected (conflict)
        assert r.status_code == 409, (r.status_code, r.text)
        j = r.json()
        assert j["ok"] is False
        assert j["message"].startswith("ERR:")

def test_init_start_result_stop_flow():
    # INIT (idempotent) + Option B: baudrate config in params
    r = post_json("/command", {"command": "INIT", "params": {"baudrate": 115200}}, 200)
    j = r.json()
    assert j["ok"] is True
    assert j["command"] == "INIT"
    assert j["message"] == "OK"
    assert_data_is_object(j)

    # START via /command should be rejected (job API)
    r = post_json("/command", {"command": "START"}, 400)
    j = r.json()
    assert j["ok"] is False
    assert j["message"].startswith("ERR:")

    # START job
    r = post_json("/start", {"timeout_ms": TIMEOUT_MS}, 202)
    j = r.json()
    assert j["ok"] is True
    assert j["message"] == "ACCEPTED"
    job_id = j["jobId"]
    assert isinstance(job_id, str) and len(job_id) > 0

    # Poll result until DONE/TIMEOUT/CANCELLED
    last = poll_until_finished(job_id)

    if last["status"] == "DONE":
        assert "qr" in last["data"]
        assert isinstance(last["data"]["qr"], str) and len(last["data"]["qr"]) > 0
        assert last["data"]["qr"].startswith("QR:")

    # STOP (dedicated endpoint)
    r = post_json("/stop", {}, 200)
    j = r.json()
    assert j["ok"] is True
    assert j["state"] == "STOPPED"
    assert j["message"] == "OK"

def test_stop_cancels_waiting_job():
    # Ensure INIT
    r = post_json("/command", {"command": "INIT"}, 200)
    assert r.json()["ok"] is True

    # Start a long timeout job, then stop immediately; expect CANCELLED or at least not DONE
    r = post_json("/start", {"timeout_ms": 20000}, 202)
    j = r.json()
    job_id = j["jobId"]
    assert isinstance(job_id, str) and len(job_id) > 0

    # Stop quickly
    r = post_json("/stop", {}, 200)
    j = r.json()
    assert j["ok"] is True
    assert j["state"] == "STOPPED"

    # Result should become CANCELLED (preferred) or TIMEOUT, but must not become DONE
    deadline = time.time() + 3.0
    last = None
    while time.time() < deadline:
        rr = get_json(f"/result/{job_id}", 200)
        last = rr.json()
        assert last["ok"] is True
        assert last["jobId"] == job_id
        assert last["status"] in ("PENDING", "DONE", "TIMEOUT", "CANCELLED")
        if last["status"] != "PENDING":
            break
        time.sleep(0.1)

    assert last is not None
    assert last["status"] != "DONE", f"job unexpectedly DONE after stop: {last}"
    assert last["status"] in ("CANCELLED", "TIMEOUT"), f"expected CANCELLED/TIMEOUT after stop, got: {last}"
