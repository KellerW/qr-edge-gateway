# CONTRACT_TESTING

This project uses **Schemathesis** to validate API behavior against the OpenAPI contract.

- OpenAPI spec: `docs/openapi.yaml`
- Contract test container: `tests/contract/Dockerfile`

## How it runs

The `contract-test` container performs:
1. Wait for `GET /health`
2. Send an idempotent `INIT` (Option B: `params.baudrate`)
3. Run Schemathesis against the mounted spec

Example run (compose):

```bash
docker compose --profile test run --rm --build contract-test
```

## Why we exclude `unsupported_method`

Crow returns HTTP 405 for unsupported methods but (in this setup) does not include the RFC-required `Allow` header for TRACE-based probing.

Schemathesis includes a check named `unsupported_method` that probes methods like TRACE and expects strict RFC behavior.  
To keep contract testing focused on the API contract itself, the contract-test excludes this check:

- `--exclude-checks unsupported_method`

## Common contract pitfalls

### 1) Undocumented status codes
If the service returns an HTTP status not listed in `docs/openapi.yaml`, Schemathesis fails with "Undocumented HTTP status code".

Fix:
- either adjust the implementation to match the contract, or
- document the status in OpenAPI (preferred if the status is valid and intended).

Example:
- `/command` may return `409` for command conflicts (busy, already running, stopped). Document `409`.

### 2) Schema-compliant request rejected
Schemathesis may generate boundary values that are valid per schema. If the server rejects them, align constraints.

Example:
- `params.baudrate` should have `minimum: 1` and `maximum: 2000000` in OpenAPI if the server enforces those bounds.

### 3) Schema-violating request accepted
If the server accepts invalid requests, add server-side validation.

Examples:
- If `params` is present, it must be an object (not null/array).
- If `params.baudrate` is present, enforce integer-only and bounds.
- If `params.timeout_ms` is present, enforce integer-only and `> 0`.

## Maintenance workflow

1. Change code (implementation)
2. Update `docs/openapi.yaml`
3. Run:
   - `api-test` (pytest)
   - `contract-test` (Schemathesis)
4. Commit the updated spec together with code changes

## Tips for stable contract tests

- Keep OpenAPI constraints aligned with server validation.
- Prefer deterministic behavior for STOP/cancel flows in CI:
  - increase `PARCEL_INTERVAL_MS` during tests, or
  - make job completion independent of immediate fake payload emission.
