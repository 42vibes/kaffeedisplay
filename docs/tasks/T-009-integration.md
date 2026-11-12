# T-009 — Firmware integration, reports, factory reset and boot

Status: review
Track: integration
Owner: T-009 integration agent
Branch: `feat/T-009-integration`

## Context

All component PRs are merged into `main`, but `firmware/main` still booted a
board-only skeleton: no LittleFS/service/UI/network, the API returned `501` for
reports, the SPA assets were not proven linked and factory reset was missing.
This task wires the complete firmware without changing frozen interfaces.

## Deliverables

1. `firmware/main/main.cpp` boot sequence:
   NVS → LittleFS `storage` → `KaffeeService` → `EspDisplay`/LVGL →
   3-page `UiApp` + `BoardDisplayPort` → `ApiServer`/`ApiController` →
   `NetworkManager` → health check → OTA valid.
2. `PlatformAdapter` (`main/platform_adapter.*`) joining `EspDisplay` and
   `NetworkManager` into `IPlatform`: stats/time/sync/network/AP/display,
   `esp_random()`-backed `random_u64()`, AP-stopped callback and stay-awake.
   Ledger IDs come from `KaffeeService::create()` with the platform RNG; the
   host-only `default_id()` is never used on the device.
3. `ApiController` worker fed by `INetworkListener`: start/stop the API when the
   AP or STA is available, never from the Wi-Fi/httpd task; stop with a grace
   delay. AP stop clears admin sessions through the service callback.
4. First boot with no users auto-starts the AP for the configured window
   (default 15 min); otherwise the AP stays off.
5. `GET /reports/{kind}` uses `kaffee::reports` for JSON and both CSV formats.
6. `POST /factory-reset` (session + CSRF + request id) erases journal,
   snapshots, settings and network credentials, then reboots; documented in
   OpenAPI.
7. Embedded webassets enabled; `/` serves the real SPA, fallback only for clean
   clones without `make assets`.
8. Settings save applies display, network and time settings; browser time sync
   reaches `NetworkManager`.
9. ADR 0013 for the additive service/platform accessors.

## Root cause found during hardware bring-up

The first full-image boot crashed in `prvSelectHighestPriorityTaskSMP` before
`app_main` (`EXCVADDR 0x44`, ready-list owner `0xa5a5a5a5`). The cause was the
host/test-only `default_id()` in `domain`: `thread_local std::mt19937_64`
reserves ~2.5 KB of TLS on every task, including the tiny IDF IPC tasks
(1280 B stacks), corrupting adjacent TCBs. The engine is now a mutex-protected
process-wide static; TLS is 0 bytes. See ADR 0011 for the device RNG policy.

A second hardware-only bug was fixed in `net`: `esp_wifi_set_config()` was
called before `esp_wifi_set_mode()` (`ESP_ERR_WIFI_MODE` on cold boot). Both AP
and STA setup now set the mode first.

## Evidence

- `make test-host`: 128 test cases / 17,282 assertions, all pass (new tests:
  ledger snapshot, settings apply hooks, factory reset, report JSON envelope).
- `make assets` + `make build-docker`: success with the real SPA
  (3 assets, 120.0 KiB gzip; app image 0x2ad760 with 11% partition free).
- `make flash` on `/dev/ttyUSB0`, serial boot log confirms: storage mounted,
  UI started on page 1, network ready, AP started for provisioning, HTTP API
  started, `boot health ... => ok`, OTA image confirmed.
- Serial boot log: `/tmp/opencode/boot4.log` from the integration run.
- Host has no Wi-Fi interface, so the API could not be exercised over the AP
  from this machine; HIL also needs the not-yet-implemented serial CLI.

## Remaining hardware checks (owner)

1. Confirm page 1 (`Buchen`) renders after boot; swipe to leaderboard and
   system page.
2. Touch all four corners and the exact screen edges; verify no offset after
   rotation changes from the admin web.
3. Measure frame rate while swiping and after a booking toast (target smooth
   60 Hz; report any visible tearing).
4. Verify dim after 60 s and screen-off after 10 min, then wake by touch.
5. Join `Kaffeekasse-XXXX`, open `http://192.168.4.1`, log in with the setup
   password, change it, create a user/product and download a report CSV.
6. Verify captive probe redirect from a phone while the AP is active.
7. Run `python3 tools/hil.py --port /dev/ttyUSB0 --allow-writes --reset` after
   the serial CLI task is implemented.

## Files

`firmware/main/**`, `firmware/components/service/**`,
`firmware/components/api/**`, `firmware/components/reports/**`,
`firmware/components/domain/src/domain.cpp`,
`firmware/components/net/src/net_manager.cpp`, `docs/api/**`, `docs/adr/0013*`,
`docs/tasks/**`, `tests/host/**`.
