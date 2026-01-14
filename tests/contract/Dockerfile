FROM python:3.12-slim

WORKDIR /work
RUN pip install --no-cache-dir schemathesis

# Schemathesis vai ler a spec via volume (montada no compose)
CMD ["sh", "-c", "schemathesis run /spec/openapi.yaml --url http://qr-c:8080 --checks all --exclude-checks unsupported_method --exclude-path-regex '^/result/.*'"]




