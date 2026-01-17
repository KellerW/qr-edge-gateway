# qr-edge-gateway — Fake QR Reader (Serial Emulation + REST API)

This repository provides a **containerized fake QR reader** used to validate end-to-end flows where a device reads QR codes from a serial-like interface and exposes results via a REST API.

It includes:

- **fake-serial**: TCP-based simulator that emits QR payload frames.
- **qr-c**: C++ service that bridges TCP → local PTY and exposes a REST API.
- **api-test**: pytest-based black-box tests.
- **contract-test**: Schemathesis-based OpenAPI contract testing.

## Quick Start

Build and run the core services:

```bash
docker compose up -d --build fake-serial qr-c
```

Verify readiness:

```bash
curl -s http://127.0.0.1:8080/health
curl -s http://127.0.0.1:8080/status
```

Initialize (INIT) and run a job:

```bash
# INIT (Option B: baudrate via params)
curl -s -X POST http://127.0.0.1:8080/command \
  -H 'Content-Type: application/json' \
  -d '{"command":"INIT","params":{"baudrate":115200}}'

# START (JSON body required; at least {})
curl -s -X POST http://127.0.0.1:8080/start \
  -H 'Content-Type: application/json' \
  -d '{"timeout_ms":3000}'
```

Poll the result (replace `<jobId>`):

```bash
curl -s http://127.0.0.1:8080/result/<jobId>
```

Stop (cancels active waits and stops the core):

```bash
curl -s -X POST http://127.0.0.1:8080/stop
```

## API

OpenAPI spec:

- `docs/openapi.yaml`

Endpoints:

- `GET /health` → `{ ok, message }`
- `GET /status` → `{ ok, state }`
- `POST /command` → sync commands (`PING`, `INIT`, `STOP`)
- `POST /start` → async job start (JSON body required)
- `GET /result/{id}` → `PENDING | DONE | TIMEOUT | CANCELLED`
- `POST /stop` → cancels active wait + stops core

## Configuration (key env vars)

### qr-c
- `SERIAL_PORT=/tmp/ttyS1`
- `FAKE_SERIAL_HOST=fake-serial`
- `FAKE_SERIAL_PORT=7000`
- `REST_BIND=0.0.0.0`
- `REST_PORT=8080`
- `READ_TIMEOUT_MS=3000`
- `REOPEN_DELAY_MS=1000`

### fake-serial
- `FAKE_SERIAL_PORT=7000`
- `PARCEL_PAYLOAD="QR:123456"`
- `PARCEL_INTERVAL_MS=2000`

## Testing

### API tests (pytest)

```bash
docker compose --profile test run --rm --build api-test
```

### Contract tests (Schemathesis)

```bash
docker compose --profile test run --rm --build contract-test
```

Notes:
- Contract tests run an INIT pre-step.
- Schemathesis excludes the `unsupported_method` check (Crow 405 does not include RFC-required `Allow` header for TRACE).

## Diagrams (PlantUML → SVG)

The authoritative diagram sources are PlantUML files under `diagrams/`.

GitHub does not render PlantUML sources directly, therefore we **render SVGs** and keep them under `diagrams/out/` so they are visible in the repository UI and inside this README.

### Render to SVG (recommended)

Using Docker (no local install required):

```bash
mkdir -p diagrams/out
docker run --rm -v "$PWD/diagrams:/work" plantuml/plantuml:latest \
  -tsvg -o out /work/*.plantuml
```

Or use the helper script:

```bash
./render_diagrams.sh
```

### Embedded diagrams (SVG)

> These images will appear after you render and commit `diagrams/out/*.svg`.

#### Architecture overview

![Architecture overview](diagrams/out/architecture_overview_v2.svg)

#### Serial emulation dataflow (DEV)

![Serial emulation dataflow](diagrams/out/serial_emulation_dataflow.svg)

#### REST job sequence

![REST job sequence](diagrams/out/rest_job_sequence.svg)

#### Core state machine

![Core state machine](diagrams/out/core_state_machine.svg)

#### Compose deployment view

![Compose deployment](diagrams/out/compose_deployment.svg)

#### Testing pipeline (profiles)

![Testing pipeline](diagrams/out/testing_pipeline.svg)

## Troubleshooting (common)

### `/start` returns `ERR:NOT_INIT`
Run INIT first:

```bash
curl -s -X POST http://127.0.0.1:8080/command \
  -H 'Content-Type: application/json' \
  -d '{"command":"INIT"}'
```

### Job times out (TIMEOUT)
- Check logs:
```bash
docker compose logs -f fake-serial
docker compose logs -f qr-c
```
- Increase `timeout_ms` in `/start`
- Ensure `/tmp/ttyS1` exists inside `qr-c`:
```bash
docker compose exec qr-c sh -lc 'ls -l /tmp/ttyS1 || true'
```

## Documentation set (recommended)

If you want a minimal, clean docs set under `docs/`:

- `docs/ARCHITECTURE.md` — rationale (PTY namespaces, dispatcher model).
- `docs/PROTOCOL.md` — framing (COBS + 0x00), payload expectations (`QR:...`), limits.
- `docs/OPERATIONS.md` — common curl flows, env vars, logs.
- `docs/CONTRACT_TESTING.md` — Schemathesis notes and OpenAPI maintenance.
