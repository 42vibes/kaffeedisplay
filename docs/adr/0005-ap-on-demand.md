# ADR 0005 — AP on demand only

Status: accepted (2026-11-11)

## Context

An always-on hotspot is unnecessary, draws power and expands the attack surface.
Admins may have an STA uplink, or may need direct access when the LAN is down.

## Decision

The AP is off by default. It starts from device page 3 with a 3 s long-press (or
an authenticated LAN request), runs for a bounded window (default 15 min), can
be extended/stopped, and turns off automatically. Captive DNS/portal run only
while it is active. HTTP/mDNS start only when an interface is up. First boot
auto-starts a 15 min provisioning window.

## Consequences

- No permanent SSID; reduced exposure.
- The admin must be physically near the device when no STA/LAN is available.
- QR codes on page 3 are the primary connection path; captive portals are
  best-effort.
