# T-001 — Domain, storage, service implementation

Status: ready
Track: backend
Owner: –
Branch: `feat/T-001-domain-storage`

## Context

The domain layer and interfaces are in `firmware/components/domain` and
`firmware/components/service`. Host tests already cover core ledger behaviour.
This task completes persistence and the service facade so API/UI can integrate.

## UI dependency (T-004)

The device UI hides and blocks guest sales when `AppSettings.guest_enabled` is
false. `KaffeeService::guest_sale()` and the ledger must enforce the same rule so
the UI check cannot be bypassed (API/serial or stale UI state).

## Deliverables

1. File-backed `IEventStore` implementation (host) + LittleFS implementation
   (device) with:
   - append + fsync, snapshot A/B, compaction, boot replay, corrupt-tail
     quarantine, storage info.
   - snapshot format `{version,last_seq,head_hash,events[]}`.
   - crash-safe rotation.
2. `ISettingsStore` for NVS (device) and a JSON/in-memory store (host); secrets
   excluded from backups.
3. `KaffeeService` implementation:
   - mutex-serialized mutations, idempotency via `X-Request-Id`.
   - construct the `Ledger` with an `IdFn` backed by
     `IPlatform::random_u64()` (`prefix + "_" + 16 lowercase hex chars`).
     `Ledger()`'s default generator is host/test-only; see ADR 0011.
   - current time from `IPlatform`, with `TimeSource`.
   - single-admin auth: PBKDF2-HMAC-SHA256, sessions, rate limiting, CSRF token.
   - `settings()` must return a copy with secrets cleared
     (`admin.password_hash`, `admin.salt`, `network.ap_password`,
     `network.sta_password`). Keep the frozen `KaffeeService` interface; do not
     add a separate public-settings accessor without an ADR.
   - settings save/apply, AP/time delegation, backup export/restore.
   - change callback coalescing (never call LVGL from the service thread).
   - max debt enforcement from settings (0 = unlimited).
4. Host tests for storage crash consistency and service auth/idempotency.

## Files

`firmware/components/storage/**`, `firmware/components/service/**`,
`tests/host/**`, `docs/data-model.md` (if details change).

## Acceptance

- `make test-host` green with truncation-at-every-byte and auth tests.
- No ESP-IDF dependency in `domain`.
- Service compiles for host (via an adapter test) and the interface matches
  `docs/api/openapi.yaml`.
- No secrets in backup JSON.

## Test plan

- power-loss simulation, corrupted hash, duplicate request, storage full
- login rate limit, session expiry, password change, CSRF token
- settings round-trip, debt limit, time quality propagation

## Evidence

<!-- PR link, test output, notes -->
