FROM python:3.12-slim

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl \
 && rm -rf /var/lib/apt/lists/*

COPY requirements.txt /app/requirements.txt
RUN pip install --no-cache-dir -r /app/requirements.txt

COPY gateway.py /app/gateway.py

# Container-friendly: log folder exists, CA cert is fetched if missing, then run
CMD ["sh", "-lc", "\
  mkdir -p /var/log/gateway-py && \
  touch \"${LOG_PATH:-/var/log/gateway-py/gateway.log}\" && \
  if [ ! -f \"${MQTT_CERT:-/tmp/broker.emqx.io-ca.crt}\" ]; then \
    echo \"[gateway-py] downloading broker.emqx.io CA to ${MQTT_CERT:-/tmp/broker.emqx.io-ca.crt}\"; \
    curl -fsSL \"https://assets.emqx.com/data/broker.emqx.io-ca.crt\" -o \"${MQTT_CERT:-/tmp/broker.emqx.io-ca.crt}\"; \
  fi && \
  python -u /app/gateway.py \
"]
