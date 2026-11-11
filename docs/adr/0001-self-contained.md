# ADR 0001 — 100% self-contained, offline-first

Status: accepted (2026-11-11)

## Context

A coffee fund must work in a kitchen with flaky or absent Wi-Fi, and must not
depend on a cloud account, external database or vendor service.

## Decision

All logic, storage, UI and the admin backend run on the ESP32-S3. The only
external dependency is optional Wi-Fi for SNTP and LAN admin access. Nothing in
the runtime path calls the internet. Firmware and admin assets are versioned
together.

## Consequences

- A local AP and manual URL/QR are required for offline admin access.
- Backups are manual downloads; there is no cloud sync.
- Time must be supplied by the admin browser when no internet is available.
- Storage, journal compaction and recovery must be robust for days offline.
