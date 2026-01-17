FROM python:3.12-slim

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
 && rm -rf /var/lib/apt/lists/*

COPY requirements.txt /app/requirements.txt
RUN pip install --no-cache-dir -r /app/requirements.txt

COPY gateway.py /app/gateway.py

# Optional: create log dir at build time
RUN mkdir -p /var/log/gateway-py

CMD ["python", "-u", "/app/gateway.py"]
