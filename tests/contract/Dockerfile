FROM python:3.12-slim

WORKDIR /work

# Schemathesis + requests (usado pelo step de INIT antes do contract run)
RUN pip install --no-cache-dir schemathesis requests

# A spec será montada em /spec/openapi.yaml via volume no compose
CMD ["sh", "-lc", "python - <<'PY'\nimport time\nimport requests\nbase='http://qr-c:8080'\nfor _ in range(50):\n    try:\n        if requests.get(base+'/health', timeout=1).status_code == 200:\n            break\n    except Exception:\n        pass\n    time.sleep(0.1)\nrequests.post(base+'/command', json={'command':'INIT','params':{'baudrate':115200}}, timeout=2)\nPY\nschemathesis run /spec/openapi.yaml --url http://qr-c:8080 --checks all --max-examples 50 --exclude-method TRACE\n"]

