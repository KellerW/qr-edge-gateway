# Serial Emulation + Command Dispatcher (Fake QR Reader) — Architecture Notes

This document describes the architecture and operational constraints of the **Fake QR Reader serial emulation** and the **single-threaded command dispatcher** used in this project.

It is intended to be referenced from the main `README.md` (or placed under `docs/`), and to help with:
- onboarding and troubleshooting,
- understanding why PTY sharing across containers fails,
- extending the system (real serial, more protocols, concurrency, CI).

---

## Scope

This document covers **two tightly related concerns**:

1. **Serial Emulation between Docker containers**
   - How a “serial-like device path” is presented to the consumer container.
   - Why PTY sharing via volumes does not work in Docker.

2. **Hexagonal architecture + single-threaded dispatcher**
   - How REST and Serial inputs safely drive the same stateful domain core.

---

## Component Overview

### Services (logical)

- **fake-serial**  
  A simulator that emits QR payloads (or protocol frames) as a byte stream over TCP.

- **qr-c**  
  The consumer container that runs:
  - a **local PTY** (created inside this container),
  - a **bridge** (`socat`) between TCP and PTY,
  - the **C++ application** which opens the PTY path (e.g., `/tmp/ttyS1`) and reads it like a serial stream.

---

## Why “PTY sharing via volume” fails

A naive design creates a PTY in `fake-serial` and shares `/tmp/ttyS1` via a volume. This fails due to Linux namespaces:

- `socat pty` creates PTYs under `devpts` (`/dev/pts/N`).
- Each container has its own `devpts` instance (namespaced `/dev/pts`).
- The `link=/tmp/ttyS1` is only a **symlink** to `/dev/pts/N` *inside the container that created it*.
- Sharing `/tmp` shares the symlink text, not the underlying PTY device node.
- Bind-mounting `/dev/pts` and `/dev/ptmx` across containers is fragile and can break container startup.

**Conclusion:** A PTY device is not a portable/shared resource across containers. The PTY must exist in the same container where the application opens it.

---

## Selected Design

### Decision
Treat “serial” as a **byte-stream transport problem**, not a shared device problem.

- Container-to-container transport uses **TCP** (Docker-native).
- The consumer container creates the local serial endpoint as a **PTY**.
- `socat` bridges the TCP stream to that PTY.

### Runtime Data Flow

```mermaid
flowchart LR
  subgraph Fake["fake-serial container"]
    F[TCP server\n(emits QR stream)]
  end

  subgraph C["qr-c container"]
    S[socat bridge\nTCP <-> PTY]
    P["PTY device\n/tmp/ttyS1"]
    A["C++ app\nopens /tmp/ttyS1"]
  end

  F -- TCP:7000 --> S
  S -- local PTY --> P
  A -- read bytes --> P
```

---

## Hexagonal Architecture + Single-threaded Command Dispatcher

### Rationale
The system has multiple concurrent inputs (REST + Serial) that must drive the same stateful behavior reliably.

We use:
- **Ports & Adapters (Hexagonal)** for clean boundaries.
- A **single-threaded Dispatcher** to serialize domain execution.

Benefits:
- deterministic ordering of state transitions,
- no mutexes in the domain core (core is called only by the dispatcher thread),
- simpler tests (core is pure logic).

### Component Interaction

```mermaid
flowchart TB
  subgraph Adapters["Adapters"]
    R[REST Adapter\n(Crow)]
    S[Serial Adapter\n(termios, reconnect)]
  end

  D[Dispatcher\n(single execution thread)]
  C[Core\n(StateMachine + CommandHandler)]
  JR[JobRunner\n(background job)]
  JS[JobStore\n(in-memory results)]

  R --> D
  S --> D
  D --> C
  C --> D

  D --> JR
  JR --> JS
  R --> JS
```

> Note: the exact wiring depends on which commands are synchronous vs. asynchronous (e.g., `/start` spawns a job and `/result/{id}` polls JobStore).

---

## Compose-level Implementation (reference)

Typical pattern:

- `fake-serial` runs a TCP server on port `7000`.
- `qr-c` starts a `socat` bridge that creates `/tmp/ttyS1` locally and forwards bytes from `fake-serial:7000`.

Example bridge command (inside `qr-c` container):

```sh
socat pty,raw,echo=0,link=/tmp/ttyS1 tcp:fake-serial:7000
```

---

## Operational Notes

### Environment variables (recommended)
Use environment variables to keep configuration stable and CI-friendly:

- `SERIAL_PORT=/tmp/ttyS1`  
  Path opened by the C++ app.

- `FAKE_SERIAL_HOST=fake-serial`  
  Docker service name.

- `FAKE_SERIAL_PORT=7000`  
  TCP port exposed by fake-serial.

- `REST_BIND=0.0.0.0`
- `REST_PORT=8080`
- `READ_TIMEOUT_MS=3000`

---

## Known Limitations / Trade-offs

1. **PTY exists only inside the consumer container**
   - By design, `/tmp/ttyS1` is local to `qr-c`.

2. **Not perfect parity with real USB serial devices**
   - Some `/dev/ttyUSB*` line-discipline quirks will not be reproduced exactly.

3. **JobStore is in-memory**
   - Results are lost when the container restarts (no persistence).

4. **Single job policy (current JobRunner design)**
   - Only one job at a time; starting a new job stops the previous one.

5. **Backpressure not yet explicit**
   - Dispatcher queue is unbounded in the current sample; production should define queue limits and behavior on overload.

---

## Troubleshooting (common failures)

- **`/tmp/ttyS1` exists but app cannot open it**
  - Ensure `socat` is running in the same container as the app.
  - Ensure file permissions allow read/write inside the container.

- **`/tmp/ttyS1 -> /dev/pts/N` points to a missing PTY**
  - This typically happens when the PTY was created in a different container. Create PTY in the consumer container.

- **Container startup fails when mounting `/dev/pts` or `/dev/ptmx`**
  - Remove those mounts; rely on TCP transport + local PTY.

---

## Future Improvements

### Serial / Transport
- Add **framing/protocol** support (e.g., newline-delimited, STX/ETX, checksum).
- Add **fault injection** knobs in fake-serial (drop, delay, jitter, disconnect).
- Add **reconnect/backoff** logic in the SerialAdapter.

### Dispatcher / Domain
- Add **queue capacity** + overflow policy (reject with `BUSY`, drop, or block with timeout).
- Add **metrics** (queue depth, command latency, job duration).
- Add **structured error taxonomy** (stable error codes + mapping to HTTP).

### Jobs
- Support **multiple concurrent jobs** (map id -> worker) or a job pool.
- Persist JobStore (Redis / SQLite) if job results must survive restarts.
- Make job results richer (timestamps, duration, error details, raw frames).

### Observability
- Add request/response logging with correlation IDs (jobId).
- Export Prometheus metrics (optional).

### CI / Code Standards
- Run `format-check` in CI (clang-format).
- Add optional `clang-tidy` gate for PRs (using `compile_commands.json`).

---

## Where to place diagrams

If you already have a general architecture diagram under `/diagrams`, add two small diagrams here (or reference them from the general diagram):

- `diagrams/serial-emulation-dataflow.(png|svg)`  
  PTY-in-consumer + TCP bridge flow.

- `diagrams/ports-adapters-dispatcher.(png|svg)`  
  Hexagonal architecture boundaries + dispatcher.

---

## Suggested README.md integration

In the main `README.md`, add a short section like:

> **Architecture note:** See `docs/serial-emulation-and-dispatcher.md` for design rationale, limitations, and extension points.

