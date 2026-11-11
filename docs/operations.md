# Operations

## First boot / provisioning

1. Device has no users and no network. Page 1 shows `Keine Benutzer`.
2. Page 3 → `WLAN starten` (3 s long-press). First boot auto-starts the AP for
   15 minutes as a provisioning convenience.
3. Join `Kaffeekasse-XXXX` with the QR or password shown on page 3.
4. Open `http://192.168.4.1` (or scan the URL QR).
5. Log in with the initial admin password shown on the setup screen; change it.
6. Create members and products; optionally configure STA Wi-Fi.
7. Stop the AP (or let it time out). The device returns to offline operation.

The AP is never on permanently. Captive portal support is best-effort; the QR
and manual IP are the reliable paths.

## Samsung / Android / iPhone connection notes

These are admin-only concerns and are not shown on the booking screen.

- Samsung One UI may say “No internet” and auto-switch to mobile data. Disable
  “Switch to mobile data” in Intelligent Wi-Fi, or choose “Stay connected”.
- Private DNS / Secure DNS may bypass the captive DNS hijack. Use the manual URL
  `http://192.168.4.1` if the portal does not appear.
- Browser “HTTPS only” mode must be off for the local HTTP page.
- iOS usually shows a “Sign in to network” sheet; if not, open the URL manually.
- If the device has an STA uplink, use the LAN address and skip the AP entirely.
  Optional NAPT can give AP clients internet so phones keep the connection.

## Time

- The admin browser sets time on login and every ~5 minutes. A manual
  `Zeit synchronisieren` button exists in System settings.
- SNTP is used if STA has internet.
- With USB attached, `python3 tools/settime.py` sets the clock over the serial
  CLI (see `docs/serial-cli.md`).
- If the device is powered but offline for days, time stays accurate from the
  last sync minus small crystal drift.
- If power is lost, the clock is restored as `restored`/`unset`. Bookings still
  work; reports flag estimated timestamps until the next admin sync.
- An RTC module or LiPo is optional hardware for better offline time; not v1.

## Backup

- Admin → System → Backup downloads a versioned JSON file (no secrets).
- Restore validates the file and writes a new snapshot; keep a copy before
  restoring.
- Recommended: download a backup after large changes and before OTA.
- The device warns in the dashboard when the last backup is older than 30 days.

## OTA

- Admin → System → Firmware upload (`firmware.bin` from CI/release).
- Dual partition, SHA-256 check, boot health check, automatic rollback.
- USB flashing is the recovery path: `make flash`.
- Keep the device on a stable 5 V supply during OTA.

## USB / offline periods

- USB is not required at runtime. The device can run for days without network.
- Serial console: logs, `status`, `time set`, `backup`, `factory-reset`. The full
  line protocol and HIL-only commands are documented in `docs/serial-cli.md`.
- If unplugged from power, it stops; time is restored/resynced on next boot.
- Journal compaction and storage-full handling work offline. When storage is
  critically full, new bookings are rejected with a clear message; admin must
  export/compact.

## Build, assets and release

- `make web` builds the admin SPA. `make assets` embeds `web/dist` as
  deterministic gzip data in the `webassets` component; generated files stay out
  of git. `make build` runs both.
- `make fonts` regenerates the LVGL Subset fonts (Inter/Montserrat, German
  umlauts, `€`, digits, punctuation) with pinned `lv_font_conv@1.5.3` into
  `firmware/components/ui/generated/`. Sources are pinned and checksummed under
  `firmware/components/ui/fonts/` (gitignored).
- `make check` runs format, OpenAPI lint, relative-link checks, host/web tests,
  the web build, asset generation and size budgets. On a machine without
  `idf.py` it skips the firmware build; CI/Docker builds the firmware.
- Releases are built by `.github/workflows/release.yml` from `v*` tags. The
  GitHub release contains `firmware.bin`, `firmware.bin.sha256`,
  `manifest.json` (commit, IDF image, web asset sizes, checksums) and a
  changelog.
- `make hil` runs the hardware smoke test when a board is at `/dev/ttyUSB0`;
  see `docs/testing.md`.

## Hardware smoke test

```sh
make flash                 # or: python3 tools/hil.py --flash ...
HIL_ADMIN_PASSWORD='...' make hil
```

The script resets the board, waits for the `Kaffeekasse booting` banner, starts
the AP over the serial CLI if needed, then exercises login, temp user/product,
purchase, topup, void and CSV export, and cleans up. It skips clearly when the
serial port is absent; if the board runs different firmware the boot-banner
check fails instead of pretending the test passed.

## Recovery

- **Admin password lost**: serial CLI reset (physical access) or factory reset;
  commands are in `docs/serial-cli.md`.
- **AP won't start**: check page 3 status/logs; reboot; USB serial diagnostics.
- **Display black**: verify power, then USB logs; ST7701 init depends on board
  revision. Keep the last known-good rotation in NVS.
- **Corrupt journal**: device boots from the last valid event and quarantines the
  tail; export logs and restore a backup if needed.
- **Boot loop**: after 3 crashes, AP-only diagnostics mode starts.

## Display settings

- Rotation and brightness are set in the admin web and applied live.
- If a rotation makes the screen unusable, revert from the web (network is
  unaffected). The device returns to page 1 after a change.
- Active/idle brightness and timeouts persist in NVS.
- While the AP is active, the QR screen stays at active brightness.
