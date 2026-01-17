# OPERATIONS

Common day-to-day commands for running, observing, and debugging the system.

## Start (DEV)

```bash
docker compose up -d --build fake-serial qr-c
docker compose logs -f qr-c
```

Health & status:

```bash
curl -s http://127.0.0.1:8080/health
curl -s http://127.0.0.1:8080/status
```

## Typical REST flow

INIT (idempotent) with baudrate:

```bash
curl -s -X POST http://127.0.0.1:8080/command \
  -H 'Content-Type: application/json' \
  -d '{"command":"INIT","params":{"baudrate":115200}}'
```

PING:

```bash
curl -s -X POST http://127.0.0.1:8080/command \
  -H 'Content-Type: application/json' \
  -d '{"command":"PING"}'
```

START (JSON body required, at least `{}`):

```bash
curl -s -X POST http://127.0.0.1:8080/start \
  -H 'Content-Type: application/json' \
  -d '{"timeout_ms":3000}'
```

Poll:

```bash
curl -s http://127.0.0.1:8080/result/<jobId>
```

STOP:

```bash
curl -s -X POST http://127.0.0.1:8080/stop
```

## Logs

Recommended container-first logging strategy:
- logs to stdout/stderr by default (Docker/Compose/K8s friendly)
- optional file logging only when a volume is mounted

View logs:

```bash
docker compose logs -f fake-serial
docker compose logs -f qr-c
```

## PTY verification (inside qr-c)

```bash
docker compose exec qr-c sh -lc 'ls -l /tmp/ttyS1 || true'
```

## Tests

API tests:

```bash
docker compose --profile test run --rm --build api-test
```

Contract tests:

```bash
docker compose --profile test run --rm --build contract-test
```

Run both:

```bash
docker compose --profile test up --build --abort-on-container-exit api-test contract-test
```

## Render diagrams (PlantUML → SVG)

```bash
./render_diagrams.sh
```

This generates `diagrams/out/*.svg` which can be committed for GitHub rendering.
