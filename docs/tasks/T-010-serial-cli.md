# T-010 — Serial CLI and live AP validation

Status: review
Track: tooling / integration
Owner: T-010 serial-cli agent
Branch: `feat/T-010-serial-cli` (from `main` 4cd69bb)

## Context

`docs/serial-cli.md` and `tools/settime.py`/`tools/hil.py` documented a line
protocol, but the firmware had no console task. Recovery, `settime` and HIL write
tests therefore could not run against a real board.

## Deliverables

1. `firmware/main/serial_console.{hpp,cpp}`: bounded UART0 line reader
   (115200 8N1, 512-byte limit, CR ignored), replies only via `OK`/`ERR` lines.
   The task never calls LVGL; commands go through `KaffeeService`/`IPlatform`.
2. Commands: `help`, `status`, `ap`/`ap start|stop|extend`, `setup-password`,
   `time`/`time set`, `backup`, `reboot`, `factory-reset`, and the HIL-only
   `book`/`void` plus `unlock-writes`.
3. Secrets: `status` never prints AP/admin secrets; `ap` prints the AP password
   only while the AP is active; `setup-password` prints only while the first-run
   password is still valid. Command lines are not logged.
4. Destructive reset: current admin password + explicit `YES` within 30 s;
   erases ledger/settings/network NVS and reboots to first boot.
5. HIL: `book`/`void` are locked by default and unlocked for 10 minutes with
   `unlock-writes <admin_password>` or at build time with
   `HIL_ALLOW_WRITES=1` / `KAFFEE_SERIAL_ALLOW_WRITES=ON`. `tools/hil.py`
   unlocks automatically in write mode.
6. Docs updated (`docs/serial-cli.md`).

## Live validation (real board, /dev/ttyUSB0)

- Flash + console: `status` reported storage mounted, heap/PSRAM, firmware,
  AP/STA, time quality; `ap` printed SSID/password/IP/remaining while active;
  `setup-password` returned the first-run password.
- `wlan0` joined `Kaffeekasse-B834`; login with the setup password returned
  `must_change_password=true`; forced change to `Kaffee-Test-2026` returned 204;
  the next login had `must_change_password=false`.
- Created a temp user/product, topped up 500 ct, checked `/users`,
  `/stats/summary`, `overview`/`consumption` `csv_excel` (BOM + `sep=;` + CRLF),
  `/device` storage/backup fields, `/backup` (no secrets), `/` real SPA and
  `/generate_204`/`/hotspot-detect.html`/`/connecttest.txt`/`/ncsi.txt`.
- `unlock-writes` + `book` created `evt_254f324dcbfe98b4` (`drinks_today=1`);
  `void` restored the user to 500 ct and the report to 0 sales/1 Storno;
  archived the user and deleted the product.
- `factory-reset` cancellation (`no`) left data intact; the confirmed reset
  replied `OK factory-reset`, rebooted with `time=0 source=unset`, no users, a
  new setup password and a new AP password, and the provisioning AP active.
- `make hil` read-only passed against the live AP.

## Known issue found during validation

`ap stop` (and the synchronous AP-stop path in
`NetworkManager::factory_reset_credentials()`) can hang while holding the
network mutex on this board: the command never replies and subsequent
`status` calls block too. The CLI factory reset therefore erases the
`kaffee_net` NVS namespace directly and reboots instead of calling
`factory_reset_credentials()`; the reboot stops the AP. This needs a separate
net/API fix (T-004/T-005 follow-up). `settime.py --status` and HIL read-only are
unaffected.

Also: `stats.drinks_today` counts purchase events even after they are voided
(`1` after the test `void`); reports and balances correctly net the reversal.

## Evidence

- `make test-host`: 138 cases / 17,412 assertions pass.
- `make check`: format, OpenAPI lint, docs links, host/web tests, web build,
  assets and size budgets pass.
- `make build-docker` (ESP-IDF 5.5.6): app image 0x2b6cc0, 10% partition free.
- `make flash` on `/dev/ttyUSB0`; boot log confirms storage, UI, network,
  HTTP API, serial console ready, health check ok.
- Live curl/serial transcripts captured in `/tmp/opencode/t010-*.{log,txt,json}`
  during the run (secrets redacted; final setup/AP values reported to the owner).
