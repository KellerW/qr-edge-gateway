FROM python:3.12-slim

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl \
 && rm -rf /var/lib/apt/lists/*

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY gateway.py .

# Baixa o certificado do Mosquitto (CA) no runtime, se não existir.
# (URL apenas em code block, conforme boas práticas)
CMD sh -lc '\
  mkdir -p /var/log/gateway-py && \
  touch "${LOG_PATH:-/var/log/gateway-py/gateway.log}" && \
  if [ ! -f "${MQTT_CERT:-/tmp/mosquitto.org.crt}" ]; then \
    echo "[gateway-py] downloading mosquitto.org.crt to ${MQTT_CERT:-/tmp/mosquitto.org.crt}"; \
    curl -fsSL "https://test.mosquitto.org/ssl/mosquitto.org.crt" -o "${MQTT_CERT:-/tmp/mosquitto.org.crt}"; \
  fi && \
  python -u gateway.py \
'
