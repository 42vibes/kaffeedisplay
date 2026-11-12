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

Implementation notes (post-review):

- Host storage: `JournalEventStore` (portable core) + `FileEventStore` (host
  filesystem backend). Journal is `journal.jsonl`; snapshots are
  `snapshot.a.json` / `snapshot.b.json`; corrupt tails are copied to
  `journal.jsonl.corrupt` best-effort and the journal is truncated in place
  (`ftruncate` + fsync). A failed `.corrupt` dump or full filesystem does not
  fail boot; appends are refused until a later successful load if truncation
  fails.
- Appends track the pre-write offset. Short writes and `fflush`/`fsync` failures
  roll the file back to that offset and fsync; if rollback fails the store marks
  itself degraded and refuses further appends. Covered by a short-write fault
  test and a `/dev/full` ENOSPC test.
- Compaction writes the inactive snapshot, reads it back and replay-validates
  the complete hash chain before truncating the journal. A corrupt snapshot
  leaves the journal untouched. The service schedules compaction after 256
  journal events or at 90% partition usage.
- Device storage: `LittleFsEventStore` (`#ifdef ESP_PLATFORM`) reuses the same
  journal core and passes the explicit `storage` partition label to
  `esp_littlefs_info`. `NvsSettingsStore` keeps the settings JSON blob (including
  secrets) in NVS. Both compiled in the Docker ESP-IDF 5.5 build.
- Service: PIMPL implementation with `std::mutex`, ledger commit hook ->
  `IEventStore::append` before state, `IPlatform::random_u64()` ids, PBKDF2
  (50 000 iterations, computed outside the global mutex), sessions and rate
  limiting on monotonic uptime, CSRF separate from the session token, debt limit,
  backup without secrets, and `undo_booking` actor `ui`.
- Idempotency is checked against `find_request()` at the top of every mutation,
  before validation/state checks (including void, undo, archive, delete and debt
  limits).
- Change notifications are queued and coalesced on a dedicated notifier thread;
  the callback never runs on the mutating thread. Vector accessors return stable
  copies (frozen-interface change, ADR 0012). AP auto-off is reported through an
  additive `IPlatform::set_ap_stopped_callback` hook and clears sessions.
- A settings load failure now fails `KaffeeService::create` instead of
  regenerating credentials. `import_backup` saves settings before replacing the
  ledger, rolls back settings on failure and checks reload results.
- Initial admin password is generated on first boot and kept in RAM for the
  current boot only; `change_admin_password` clears it and writes an audit event.
  A reboot before the first change requires physical/factory reset (see
  `docs/security.md`).
- `settings_to_json(..., true)` now also emits `admin` credentials so the host
  JSON store and NVS can persist them. Backups still call
  `settings_to_json(..., false)` and never contain admin/AP/STA secrets.

`make test-host` output:

```
[doctest] test cases:    48 |    48 passed | 0 failed | 0 skipped
[doctest] assertions: 10735 | 10735 passed | 0 failed |
[doctest] Status: SUCCESS!
```

Docker firmware build (`espressif/idf:release-v5.5`, `idf.py build`): SUCCESS
(`kaffeekasse.bin`, 0x3d740 bytes, 92% app partition free).
