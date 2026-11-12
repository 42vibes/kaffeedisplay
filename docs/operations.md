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
- If the device is powered but offline for days, time stays accurate from the
  last sync minus small crystal drift.
- If power is lost, the clock is restored as `restored`/`unset`. Bookings still
  work; reports flag estimated timestamps until the next admin sync.
- An RTC module or LiPo is optional hardware for better offline time; not v1.

## Network / API integration

- `INetworkListener::on_network_state_changed()` and `on_ap_stopped()` are
  delivered from the network timer task, never from the Wi-Fi event task or an
  HTTP task. The API must hand start/stop and session cleanup to its own
  task/queue instead of blocking the callback.
- `on_ap_stopped()` fires once after a confirmed AP stop; this is the hook for
  clearing admin sessions and stopping the HTTP server.
- AP passwords and QR payloads are masked while the AP is inactive.

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
- Serial console: logs, `status`, `time set`, `backup`, `factory-reset`.
- If unplugged from power, it stops; time is restored/resynced on next boot.
- Journal compaction and storage-full handling work offline. When storage is
  critically full, new bookings are rejected with a clear message; admin must
  export/compact.

## Recovery

- **Admin password lost**: serial CLI reset (physical access) or factory reset.
- **AP won't start**: check page 3 status/logs; reboot; USB serial diagnostics.
- **Display black**: verify power, then USB logs; ST7701 init depends on board
  revision. Keep the last known-good rotation in NVS.
- **Corrupt journal**: device boots from the last valid event and quarantines the
  tail; export logs and restore a backup if needed.
- **Boot loop**: after 3 crashes, AP-only diagnostics mode starts.
- **Factory reset**: erase the `kaffee_net` NVS namespace (AP credentials and
  last known time). `NetworkManager::factory_reset_credentials()` performs this
  in-process and regenerates the AP password; otherwise erase the namespace as
  part of the storage factory reset.

## Display settings

- Rotation and brightness are set in the admin web and applied live.
- If a rotation makes the screen unusable, revert from the web (network is
  unaffected). The device returns to page 1 after a change.
- Active/idle brightness and timeouts persist in NVS.
- While the AP is active, the QR screen stays at active brightness.
