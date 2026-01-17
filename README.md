# qr-edge-gateway — Fake QR Reader (Serial Emulation + REST API)

This repository provides a **containerized fake QR reader** used to validate end-to-end flows where a device reads QR codes from a serial-like interface and exposes results via a REST API.

It is built to be:

- **Container-friendly** (logs to stdout, TCP transport between containers, no shared PTYs)
- **Deterministic** (single-threaded Core execution via a Dispatcher)
- **Testable** (pytest black-box tests + OpenAPI contract tests via Schemathesis)

## Contents

- [Quick Start](#quick-start)
- [What runs where](#what-runs-where)
- [API](#api)
- [Testing](#testing)
- [Diagrams](#diagrams)
- [Documentation](#documentation)
- [Troubleshooting](#troubleshooting)

---

## Quick Start

Start the core services:

```bash
docker compose up -d --build fake-serial qr-c
```

Verify readiness:

```bash
curl -s http://127.0.0.1:8080/health
curl -s http://127.0.0.1:8080/status
```

Run a standard flow (INIT → START → RESULT):

```bash
# INIT (Option B: set baudrate via params)
curl -s -X POST http://127.0.0.1:8080/command \
  -H 'Content-Type: application/json' \
  -d '{"command":"INIT","params":{"baudrate":115200}}'

# START (JSON body required; at least {})
curl -s -X POST http://127.0.0.1:8080/start \
  -H 'Content-Type: application/json' \
  -d '{"timeout_ms":3000}'
```

Poll result (replace `<jobId>`):

```bash
curl -s http://127.0.0.1:8080/result/<jobId>
```

Stop (cancel + stop core):

```bash
curl -s -X POST http://127.0.0.1:8080/stop
```

---

## What runs where

- **fake-serial**: emits a QR payload stream over TCP (`:7000`)
- **qr-c**:
  - creates a **local PTY** (e.g., `/tmp/ttyS1`) inside this container
  - bridges TCP → PTY using `socat`
  - runs the C++ service:
    - REST API (Crow)
    - Core (state machine)
    - Dispatcher (single worker thread)
    - JobRunner + JobStore (async job + polling)

Key constraint: **do not share PTYs across containers**. PTYs live in `/dev/pts` and are namespaced per container; a `/tmp/ttyS1` link created in one container is not usable in another.

---

## API

OpenAPI specification:

- `docs/openapi.yaml`

Endpoints:

- `GET /health` — liveness
- `GET /status` — Core state (`NOT_INIT|INIT|RUNNING|STOPPED`)
- `POST /command` — synchronous commands (`PING`, `INIT`, `STOP`)
- `POST /start` — async job start (**JSON body required**)
- `GET /result/{id}` — poll job result (`PENDING|DONE|TIMEOUT|CANCELLED`)
- `POST /stop` — cancel active wait + transition to STOPPED

For detailed request/response semantics and constraints, see:
- `docs/OPERATIONS.md` (curl flows)
- `docs/PROTOCOL.md` (serial framing + payload expectations)

---

## Testing

### API tests (pytest)

```bash
docker compose --profile test run --rm --build api-test
```

### Contract tests (Schemathesis)

```bash
docker compose --profile test run --rm --build contract-test
```

Contract testing notes (why `unsupported_method` is excluded, how to keep OpenAPI aligned):
- `docs/CONTRACT_TESTING.md`

---

## Diagrams

This repository keeps PlantUML sources under `diagrams/` and **rendered SVGs** under `diagrams/out/` so diagrams are visible in GitHub.

### Render diagrams

Using Docker:

```bash
mkdir -p diagrams/out
docker run --rm -v "$PWD/diagrams:/work" plantuml/plantuml:latest \
  -tsvg -o out /work/*.plantuml
```

Or use:

```bash
./render_diagrams.sh
```

Commit the generated `diagrams/out/*.svg` so they display in GitHub.

### Diagram: architecture overview

**What it explains:** end-to-end context (cloud ↔ gateway ↔ QR adapter ↔ serial), and how DEV emulation differs from PROD serial.

![Architecture overview](diagrams/out/architecture_overview_v2.svg)

If you want the original version, also render and embed:
- `diagrams/architecture.plantuml`

### Diagram: serial emulation dataflow (DEV)

**What it explains:** why the PTY must be created in the *consumer* container (`qr-c`), and how the TCP stream becomes a local serial-like device.

![Serial emulation dataflow](diagrams/out/serial_emulation_dataflow.svg)

### Diagram: REST job sequence

**What it explains:** request/response ordering and component responsibilities (REST → Dispatcher/Core, async job in JobRunner, polling via JobStore).

![REST job sequence](diagrams/out/rest_job_sequence.svg)

### Diagram: Core state machine

**What it explains:** allowed transitions between `NOT_INIT`, `INIT`, `RUNNING`, `STOPPED` and how `/start`, job completion, and `/stop` interact.

![Core state machine](diagrams/out/core_state_machine.svg)

### Diagram: Compose deployment view

**What it explains:** ports, volumes, healthchecks, and how test profiles attach to the main services.

![Compose deployment](diagrams/out/compose_deployment.svg)

### Diagram: Testing pipeline

**What it explains:** how CI-like runs work (healthchecks → api-test + contract-test) and what the contract-test actually does.

![Testing pipeline](diagrams/out/testing_pipeline.svg)

---

## Documentation

Minimal documentation set under `docs/`:

- `docs/ARCHITECTURE.md` — rationale (PTY namespaces, dispatcher model)
- `docs/PROTOCOL.md` — framing (COBS + 0x00), payload expectations, limits
- `docs/OPERATIONS.md` — common curl flows, env vars, logs
- `docs/CONTRACT_TESTING.md` — Schemathesis notes and OpenAPI maintenance

---

## Troubleshooting

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
- Ensure the PTY exists inside `qr-c`:

```bash
docker compose exec qr-c sh -lc 'ls -l /tmp/ttyS1 || true'
```

### Contract test failures
- Ensure the mounted OpenAPI is up to date (`docs/openapi.yaml`)
- See: `docs/CONTRACT_TESTING.md`
