# Design Decisions & Rationale — Serial Emulation Between Docker Containers (Fake QR Reader)

## Context / Goal
We need a reliable way to emulate a QR-code reader that communicates over a “serial-like” interface, while keeping the system containerized (Docker Compose). The consumer (C++ application in `qr-c`) expects to open a serial device path (e.g., `/dev/tty*` or equivalent) and read data as a stream.

## Problem Statement (Why the naive PTY sharing fails)
A common first attempt is to create a PTY pair in one container (e.g., `fake-serial`) using `socat pty,...` and expose the resulting symlink(s) (e.g., `/tmp/ttyS1`) via a shared volume (`/tmp`).

This does **not** work reliably in Docker because:

- `socat pty` creates PTYs under `devpts` (e.g., `/dev/pts/N`).
- Each container has its own `devpts` instance (its own `/dev/pts` namespace).
- The `link=/tmp/ttyS1` is only a **symlink** to `/dev/pts/N` *inside the container that created it*.
- Mounting `/tmp` between containers shares the symlink, **not** the underlying PTY device.  
  The consumer container sees a link like `/tmp/ttyS1 -> /dev/pts/3`, but `/dev/pts/3` does not exist in its own namespace.
- Trying to “force” sharing `/dev/pts` and `/dev/ptmx` across containers (bind-mount) is fragile and can break container startup (e.g., `ptmx: device or resource busy`) because the runtime (runc) manages those devices during container init.

**Conclusion:** PTY devices are not a portable/shared resource across containers. Sharing a PTY via a volume is fundamentally incompatible with Docker’s isolation model.

## Selected Approach (Why we chose it)
We selected a Docker-native design:

- `fake-serial` exposes the QR reader stream over the Docker network (TCP).
- `qr-c` creates the local “serial device” (PTY) and bridges it to the TCP stream using `socat`.

This means the device file that the C++ application opens exists **in the same container namespace** where the application runs, which is the only robust way to make “serial-by-path” work in containers.

### Data flow
(fake-serial) TCP server ---> (qr-c) socat bridge: TCP <-> PTY ---> C++ app reads /tmp/ttyS1


## Key Design Decisions
### Decision 1 — Treat “serial” as a stream transport problem, not a shared device problem
Instead of trying to share a kernel TTY device across containers (which Docker is not designed for), we carry the bytes over a standard container-friendly transport: TCP.

**Rationale:**
- TCP is natively supported across containers with predictable behavior.
- Easy to instrument (logging, delays, fault injection).
- Works in CI and across developer machines without host-level privileges.

### Decision 2 — Create the PTY in the consumer container (`qr-c`)
The consumer container is where the serial device path is required. Therefore the PTY must be created there.

**Rationale:**
- The C++ application uses OS calls that expect a local device node.
- Ensures the application sees a valid TTY (termios-compatible) inside its own namespace.
- Avoids devpts namespace mismatch entirely.

### Decision 3 — Use `socat` as the bridge (PTY <-> TCP)
`socat` is a lightweight, widely available tool for connecting file descriptors, PTYs, and sockets.

**Rationale:**
- Minimal moving parts and no custom code required for the bridge.
- Well-known behavior for raw/echo settings.
- Easy to run as a background process in an entrypoint shell.

## Alternatives Considered
### Alternative A — Host virtual TTY pairs + `devices:` in Compose (Option B)
Create `/dev/tnt0` and `/dev/tnt1` on the host (e.g., using `tty0tty`) and pass each endpoint into a different container.

**Why not selected:**
- Requires host kernel module installation/configuration.
- Often blocked by Secure Boot (“Key was rejected by service”).
- Reduces portability (developers/CI runners must be prepared identically).
- Adds operational/security friction (permissions, udev rules, privileged access).

### Alternative B — Bind-mount `/dev/pts` and `/dev/ptmx` between containers
Attempt to share PTYs by mounting host `/dev/pts` into containers.

**Why not selected:**
- Fragile with Docker/runc initialization; can fail with `ptmx busy` and other runtime errors.
- Higher privilege surface area and unpredictable across host configurations.
- Not a maintainable architecture for teams/CI.

### Alternative C — Run fake device and consumer in the same container
Put both `fake-serial` and `qr-c` in one container so PTY is naturally local.

**Why not selected:**
- Reduces modularity (simulator no longer isolated).
- Harder to reuse fake device across multiple consumers.
- Less representative of a real multi-service environment.

## Benefits of the Selected Approach
- **Portability:** Runs on any host with Docker; no kernel modules or Secure Boot changes.
- **Reproducibility:** Stable behavior across dev machines and CI.
- **Isolation:** Fake device stays isolated; consumer remains independent.
- **Observability & Testing:** TCP stream is easy to log, throttle, delay, disconnect/reconnect for robustness testing.
- **Low Operational Risk:** Avoids privileged containers and host-level device dependencies.

## Known Limitations / Trade-offs
- The “serial device file” is local to the consumer container (by design).  
  The fake device is a stream provider, not a shared kernel TTY.
- If strict hardware parity is required (real `/dev/ttyUSB*` semantics, line discipline specifics), host-level device approaches may be closer—but at significant portability cost.

## Implementation Summary (Compose-level)
- `fake-serial`: TCP server emitting QR strings (or protocol frames).
- `qr-c`: `socat pty,raw,echo=0,link=/tmp/ttyS1 tcp:fake-serial:7000`
- C++ app: opens `/tmp/ttyS1` and reads from it like a serial stream.

## Outcome
This approach resolves the fundamental Docker namespace constraints (devpts isolation) while still presenting a true serial-like device path to the C++ consumer. It is therefore the most robust and maintainable solution for container-based development and automated testing.
