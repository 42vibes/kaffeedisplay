# T-010 — Serial CLI and live AP validation

Status: done
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

## Follow-up live fixes

1. **`ap stop` deadlock.** Tracing showed `stop_ap()` holding the network mutex
   while calling `mdns_free()`; the mDNS teardown waits for the Wi-Fi/event task,
   which was blocked on that same mutex. mDNS teardown is now scheduled on the
   network timer task (outside the mutex), with an `mdns_stopping` guard so a
   concurrent AP start either keeps the instance or restarts it after the
   deferred free. `ap stop` returns promptly, `status` works immediately after,
   and `ap start` recovers. This also removes the deadlock from
   `NetworkManager::factory_reset_credentials()`. The API listener/session clear
   remains deferred through `ApiController::on_ap_stopped()`.
2. **`drinks_today`.** `KaffeeService::stats()` now counts only non-voided
   `Purchase`/`GuestSale` events in the current Europe/Berlin day (correct DST
   boundaries). A host test covers purchase → 1 and void → 0; live serial and
   `/device` both showed 1 then 0.
3. **HTTP task stack overflow (found during validation).** Creating a product
   through the API overflowed the `httpd` task at 10 KiB and rebooted the board.
   The server stack is now 24 KiB; the ineffective
   `CONFIG_HTTPD_TASK_STACK_SIZE` line was removed from `sdkconfig.defaults`.


## Display scanout fix (follow-up)

The panel image was shifted right by ~55–65 px (UI left clock/content clipped,
right edge wrapped). The simulator was correct, and the type-9 test crosshair
shifted with the panel, confirming scanout rather than layout. HBP/HFP/pulse
sweeps only moved the phase by a few pixels; 16 MHz made it worse. The root
cause is a PSRAM RGB FIFO underrun: the RGB DMA occasionally fetches a line
late, so the panel latches the next line offset and wraps.

Fix: five-line internal-RAM bounce buffer (`bounce_buffer_size_px = 5 * 480`)
with the PSRAM framebuffer still enabled, plus the known-good Guition timing
`12 MHz, HSYNC 8/50/10, VSYNC 8/20/10`. Measured crosshair shift dropped from
+72.4 px to about +8 px; page dots are centred and page 1 has no edge clipping.
The HTTP server stack was reduced from 24 KiB to 16 KiB to make room for the
12 KiB serial-console stack; product creation was re-tested through the API
(201, no panic).
Final webcam frames: `/tmp/opencode/t010-display/final-test.jpg`,
`/tmp/opencode/t010-display/final-page1-bounce6.jpg`.

## Evidence

- `make test-host`: 139 cases / 17,421 assertions pass.
- `make check`: format, OpenAPI lint, docs links, host/web tests, web build,
  assets and size budgets pass.
- `make build-docker` (ESP-IDF 5.5.6): app image 0x2b6cc0, 10% partition free.
- `make flash` on `/dev/ttyUSB0`; boot log confirms storage, UI, network,
  HTTP API, serial console ready, health check ok.
- Live serial `ap stop` → `OK ap stop`, immediate `status` (AP off), `ap start`
  → `OK ap start ttl=900`, `status` (AP on); repeated after final flash.
- Live `book` → `drinks_today=1` in serial and `/device`; `void` →
  `drinks_today=0` in both. Product creation via API returned 201 in 0.12 s with
  no panic after the stack fix.
- API `POST /ap/stop` dropped the connection (expected) but the next `status`
  worked and the old session returned 401 after AP restart.
- Final factory reset left `seq=0`, `users=0`, `products=0`, `time=0
  source=unset`, AP provisioning up with fresh setup/AP passwords.
- Live curl/serial transcripts captured in `/tmp/opencode/t010*` during the run
  (secrets redacted; final setup/AP values reported to the owner).
