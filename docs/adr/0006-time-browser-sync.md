# ADR 0006 — Time from the admin browser, SNTP optional

Status: accepted (2026-11-11)

## Context

The ESP32-S3 has no battery-backed RTC. The device may be offline for days and
lose power, but monthly reports and timestamps need a reasonable clock.

## Decision

The admin SPA sends `epoch_ms` to `POST /time/sync` on login and every ~5 min.
SNTP is used only when STA has internet. Time quality (`synced`, `restored`,
`unset`) is stored per event and shown in reports. A manual sync/set control
exists in the admin System page. Events are still recorded when time is unknown
and flagged as estimated.

## Consequences

- Offline operation works; exact time across power loss requires the next admin
  connection.
- Reports can warn about estimated timestamps instead of silently misdating.
- An optional RTC/LiPo is a future hardware improvement.
