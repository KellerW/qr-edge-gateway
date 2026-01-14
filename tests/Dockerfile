FROM python:3.12-slim

WORKDIR /tests
COPY requirements.txt /tests/requirements.txt
RUN pip install --no-cache-dir -r /tests/requirements.txt

COPY test_api.py /tests/test_api.py

# defaults (podem ser sobrescritos no compose)
ENV BASE_URL=http://qr-c:8080 \
    TIMEOUT_MS=1000 \
    POLL_INTERVAL=0.3 \
    POLL_MAX_SECONDS=6.0

CMD ["pytest", "-q", "-p", "no:cacheprovider"]
