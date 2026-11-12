# T-009 — Firmware integration, reports, factory reset and boot

Status: done
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

## First-boot usability/security fix

The generated admin password previously lived only in RAM. A reboot before the
first change locked the admin out and the device UI never showed the password.
The service now persists it in a dedicated NVS settings key
(`setup_password`), outside `Settings`, backups and the journal; loads it on
boot while `must_change_admin_password()` is true; clears it after a successful
change; and keeps the force-change state until then.

`IUiBackend` exposes `must_change_admin_password()`/`setup_password()`.
Page 3 shows an `Erste Einrichtung` card with the password, AP SSID/URL and the
instruction to change it in the web UI; the page-1 empty state points to
`Seite 3 → WLAN + Passwort`. The SPA reads `must_change_password` from
login/`/auth/me` and renders a forced first-login change form before the shell.

## Review fixes

- **All-time reports:** JSON omits `from`/`to` for `period=all`; the web date
  formatter returns `–` for sentinel/invalid timestamps instead of throwing.
- **Factory reset UI:** the danger-zone button is enabled with the existing
  typed `WERKSRESET` confirmation; mock/e2e coverage added.
- **LittleFS safety:** `format_if_mount_failed=false`. Only a genuinely blank
  partition is formatted once; a failed mount aborts the boot and leaves OTA
  rollback armed. Only the blank-format path was proven on hardware; the
  corrupt-refusal path is covered by the unit-tested mount policy and log path.
- **Read-only reports:** `ledger_snapshot()` now returns
  `std::shared_ptr<const Ledger>` with the commit hook cleared.
- **Settings atomicity:** `save_settings()` persists first and rolls back the
  blob + live display/network/time on a platform apply failure.
- **Contract tests:** factory reset added to the request-id table; time sync is
  allowed during the forced password change.
- **Boot health:** includes the API server/controller. The controller retries a
  failed HTTP start every 5 s while an interface is up; boot health waits for
  the API when the AP/STA is active.
- **Nits:** stay-awake calls are idempotent/consolidated, factory-reset task
  creation failure returns 500, and OpenAPI/docs now describe all-time report
  semantics, errors and buffered CSV sending.

## Live AP login fixes

Live testing against the AP found two blockers:

1. **Empty `Set-Cookie`.** `httpd_resp_set_hdr()` stores the value pointer, and
   the cookie was formatted into a stack buffer that died when the handler
   returned. It now uses a server-lifetime buffer, rejects an empty token, and
   logs token/header lengths. Current wall time unset uses a 30-minute
   `Max-Age` fallback while sessions stay monotonic.
2. **Slow login.** The portable PBKDF2 path took ~13.6 s for 50k iterations on
   the S3. The device now uses mbedTLS's SHA-accelerated PBKDF2 at 10 000
   iterations; the portable path remains for host tests. Old 50k hashes are
   rehashed on the next successful login.

Measured over the real AP from 192.168.4.2: fresh setup-password login 0.88 s,
forced password change 1.77 s, new-password login 0.89 s. The first login of a
pre-upgrade 50k hash is a one-time 5.1 s (verification + transparent rehash).

## Evidence

- `make test-host`: 138 test cases / 17,412 assertions, all pass (new tests:
  ledger snapshot, settings apply/rollback, factory reset, report JSON envelope,
  all-time report bounds, setup-password persistence/reload/clear/upgrade,
  PBKDF2 iteration migration, service/device stats, LittleFS mount policy,
  UI strings).
- `make test-web`: typecheck + 28 Vitest tests, including the forced first-login
  form and invalid timestamp formatting.
- `make test-e2e`: 10 Playwright tests, including the typed factory-reset flow.
- `make assets` + `make build-docker`: success with the real SPA
  (3 assets, 120.4 KiB gzip; app image 0x2afd90 with 10% partition free).
- `make flash` on `/dev/ttyUSB0`, serial boot log confirms: storage mounted,
  UI started on page 1, network ready, AP started for provisioning, HTTP API
  started, `boot health ... => ok`, OTA image confirmed.
- Serial boot log: `/tmp/opencode/boot-blockers-final.log` from the final image.
  A forced blank `storage` partition was formatted once (`boot-blank.log`); the
  normal next boot mounted without formatting. The mount-failure path aborts
  boot before the health/OTA confirmation and is covered by the unit-tested
  policy. The setup password key survived storage erase, reflash and reboot
  unchanged.
- Live AP (192.168.4.2): `Set-Cookie: kaffee_session=<token>; Path=/; HttpOnly;
  SameSite=Strict; Max-Age=1800`; `/auth/me` 200; forced change 204; new login
  must_change=false; GET settings/device/users/products/summary 200; overview
  `csv_excel` 200 with BOM/`sep=;`; `/` 200 real SPA; hashed JS 200 + gzip;
  `/generate_204` 204; `/hotspot-detect.html` 302. Temp product creation was
  skipped after the host Wi-Fi link dropped; cleanup of the temporary user is
  pending on the board.
- Live `/device` after mount: `storage_mounted=true`, `storage_used=258`,
  `storage_total=9437184`; after `GET /backup`, `last_backup_at` changed from
  `0` to `1794478175`. `drinks_today=0` is correct for the empty test ledger;
  the host test covers a non-zero count (2 purchases).
- HIL still needs the not-yet-implemented serial CLI.
- Branch `feat/T-009-integration` is pushed. PR creation from this environment
  failed with `Resource not accessible by personal access token
  (createPullRequest)`; open the PR manually from the pushed branch.

## Remaining hardware checks (owner)

1. Confirm page 1 (`Buchen`) renders after boot; swipe to leaderboard and
   system page.
2. On page 3 while `Erste Einrichtung` is visible, confirm the admin password,
   AP SSID and `http://192.168.4.1/` are readable without scrolling. The card
   disappears after the web password change.
3. Touch all four corners and the exact screen edges; verify no offset after
   rotation changes from the admin web.
4. Measure frame rate while swiping and after a booking toast (target smooth
   60 Hz; report any visible tearing).
5. Verify dim after 60 s and screen-off after 10 min, then wake by touch.
6. Join `Kaffeekasse-XXXX`, open `http://192.168.4.1`, log in with the setup
   password, complete the forced password change, create a user/product and
   download a report CSV.
7. Verify captive probe redirect from a phone while the AP is active.
8. Run `python3 tools/hil.py --port /dev/ttyUSB0 --allow-writes --reset` after
   the serial CLI task is implemented.

## Files

`firmware/main/**`, `firmware/components/service/**`,
`firmware/components/api/**`, `firmware/components/reports/**`,
`firmware/components/domain/src/domain.cpp`,
`firmware/components/net/src/net_manager.cpp`, `docs/api/**`, `docs/adr/0013*`,
`docs/tasks/**`, `tests/host/**`.
