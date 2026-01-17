# PROTOCOL

This document describes the **serial framing** used between `fake-serial` and the C++ service (`qr-c`) in DEV mode.

## Transport

- DEV: TCP stream between containers (`fake-serial:7000` → `qr-c`)
- Inside `qr-c`: `socat` bridges TCP → local PTY (`/tmp/ttyS1`)
- The C++ JobRunner opens and reads the PTY as a serial-like file descriptor

## Framing

- Frames are **COBS-encoded** (Consistent Overhead Byte Stuffing)
- Frame delimiter is a **single `0x00` byte** (not included in the decoded payload)

### Read algorithm (conceptual)
1. Read bytes until `0x00` delimiter
2. COBS-decode the collected bytes (excluding the delimiter)
3. Interpret the decoded payload as ASCII/UTF-8 text

## Payload format

Expected payload (ASCII):

- `QR:<value>`

Examples:
- `QR:123456`
- `QR:ABCDEF-001`

Non-matching payloads are treated as invalid and the job continues until timeout (or returns TIMEOUT depending on policy).

## Limits and safety

Recommended constraints implemented in the JobRunner:

- **Max encoded frame size**: 4096 bytes (drop/ignore larger frames)
- Timeout: `timeout_ms` supplied by `/start` (or default)
- Re-open retry: `REOPEN_DELAY_MS` controls how often the JobRunner retries `open()` on the PTY until deadline

## Baudrate

In DEV with PTY, baudrate is largely informational (termios calls may fail or be ignored depending on the PTY implementation).  
In PROD (real serial device), baudrate matters.

The service supports **Option B** configuration via REST:

- `POST /command` with:
  - `{"command":"INIT","params":{"baudrate":115200}}`
  - or top-level `{"command":"INIT","baudrate":115200}`

Constraints (recommended in OpenAPI and server-side validation):
- integer only
- `1 <= baudrate <= 2000000`
