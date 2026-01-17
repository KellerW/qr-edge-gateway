# ARCHITECTURE

This project implements a **fake QR reader** that reads QR payloads from a serial-like stream and exposes them via a REST API.

It is designed to be **container-friendly** and **deterministic** (single-threaded Core execution).

## High-level components

- **fake-serial** (container): emits QR payloads as a byte stream over TCP.
- **qr-c** (container): creates a local PTY and bridges TCP → PTY, then runs the C++ service:
  - REST API (Crow)
  - Core state machine
  - Dispatcher (single worker thread)
  - JobRunner + JobStore (async job execution + result polling)

## Why PTY sharing across containers does not work

A PTY created by `socat pty,...` points to a device node under `/dev/pts/N` (devpts).  
In Docker, `/dev/pts` is **namespaced per container**. Sharing `/tmp/ttyS1` via a volume shares only the symlink string, not the underlying PTY.

**Rule:** the PTY must be created in the same container where the application opens it.

## Selected design (DEV)

- Container-to-container transport: **TCP**
- Consumer container (`qr-c`) creates a **local PTY** (e.g., `/tmp/ttyS1`)
- `socat` bridges the TCP byte stream to the PTY
- The C++ JobRunner opens `/tmp/ttyS1` and reads protocol frames

See rendered diagrams under `diagrams/out/`:
- `serial_emulation_dataflow.svg`
- `compose_deployment.svg`

## Ports & Adapters (Hexagonal) + single-threaded Dispatcher

### Why a Dispatcher
The Core is a state machine. Multiple concurrent inputs (REST handlers, job completion callbacks, etc.) must not call Core concurrently.

We use a single-threaded **Dispatcher** to serialize all Core calls:
- deterministic ordering of state transitions
- no locks/mutexes inside the Core
- simpler unit tests

### Execution paths
- `/command` is synchronous: REST → Dispatcher → Core → REST response
- `/start` is async: REST → Dispatcher (state transition) → JobRunner starts background wait
- `/result/{id}` is read-only polling: REST → JobStore
- `/stop` cancels any pending wait and transitions Core to STOPPED

See rendered diagrams:
- `rest_job_sequence.svg`
- `core_state_machine.svg`

## Core state model (summary)

States:
- `NOT_INIT` → initial
- `INIT` → ready to start jobs
- `RUNNING` → job running / waiting for frame
- `STOPPED` → explicitly stopped

Key transitions:
- INIT command: `NOT_INIT|STOPPED → INIT`
- START job: `INIT → RUNNING`
- Job finished (DONE/TIMEOUT/CANCELLED): `RUNNING → INIT`
- STOP: `* → STOPPED`

## Container boundaries

- **fake-serial** owns only the TCP stream (no PTY)
- **qr-c** owns PTY + REST API + domain/core
- **gateway-py** (optional) uses REST and MQTT; it should not access PTY directly
