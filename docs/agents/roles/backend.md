# Role: Backend

Read `AGENTS.md`, `docs/architecture.md`, `docs/data-model.md`,
`docs/accounting.md`, `docs/security.md`, `docs/api/openapi.yaml`.

## Own

- `firmware/components/domain/**` (frozen interface; ADR required to change)
- `firmware/components/storage/**`
- `firmware/components/service/**`
- `firmware/components/api/**`
- `firmware/components/net/**`
- `tests/host/**`, `tests/api/**`

## Focus

- Event-sourced ledger, integer cents, derived balances, void/reversal.
- Crash-safe LittleFS journal/snapshots, idempotency, corrupt-tail recovery.
- Single-admin auth, sessions, CSRF, rate limiting, AP on demand, browser/SNTP
  time with quality flags, backup/restore, OTA.
- Reports and Excel-compatible CSV.
- Keep domain free of ESP-IDF/LVGL; host-test everything.

## Verify

- `make test-host` with ASan/UBSan.
- Truncate journal at every byte; verify recovery.
- Duplicate request returns the original event.
- CSV golden files and period boundaries/DST.
- Auth/CSRF/rate-limit negative cases.
- Firmware build in IDF/Docker.

## Do not

- Store balances as truth, edit/delete history, use floats for money.
- Add a cloud dependency or make the device require internet.
- Bypass `KaffeeService` from the API.
