# T-001 — Domain, storage, service implementation

Status: review
Track: backend
Owner: subagent (T-001)
Branch: `feat/T-001-domain`

## Context

The domain layer and interfaces are in `firmware/components/domain` and
`firmware/components/service`. Host tests already cover core ledger behaviour.
This task completes persistence and the service facade so API/UI can integrate.

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

PR: https://github.com/Babo96/kaffeedisplay/pull/3

Implementation notes:

- Host storage: `JournalEventStore` (portable core) + `FileEventStore` (host
  filesystem backend). Journal is `journal.jsonl`; snapshots are
  `snapshot.a.json` / `snapshot.b.json`; corrupt tails are copied to
  `journal.jsonl.corrupt`. Appends `fflush` + `fsync`; snapshots and journal
  rotation use `tmp` + rename + directory fsync.
- Device storage: `LittleFsEventStore` (`#ifdef ESP_PLATFORM`) reuses the same
  journal core with ESP-LittleFS capacity reporting. `NvsSettingsStore` keeps
  the settings JSON blob (including secrets) in NVS. **Device adapters were not
  compiled here: this worktree has no ESP-IDF toolchain.**
- Service: PIMPL implementation with `std::mutex`, ledger commit hook ->
  `IEventStore::append` before state, `IPlatform::random_u64()` ids, PBKDF2
  (50 000 iterations), session idle/absolute expiry, exponential login backoff,
  CSRF, debt limit, backup without secrets, and `undo_booking` actor `ui`.
- Initial admin password is generated on first boot and kept in RAM for the
  current boot only; `change_admin_password` clears it and writes an audit event.
  A reboot before the first change requires physical/factory reset (see
  `docs/security.md`).
- `settings_to_json(..., true)` now also emits `admin` credentials so the host
  JSON store and NVS can persist them. Backups still call
  `settings_to_json(..., false)` and never contain admin/AP/STA secrets.

`make test-host` output:

```
[doctest] test cases:    39 |    39 passed | 0 failed | 0 skipped
[doctest] assertions: 10264 | 10264 passed | 0 failed |
[doctest] Status: SUCCESS!
```
