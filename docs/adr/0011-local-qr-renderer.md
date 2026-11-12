# ADR 0011 — Local QR rendering in the admin SPA

Status: accepted (2026-11-11)

## Context

`GET /ap` returns QR payloads as plain strings. The real firmware returns the
Wi-Fi join string (`WIFI:T:WPA;S:...;P:...;;`) and the admin URL, not data URLs.
Displaying the payload as text is not acceptable on the System page, and the
runtime must stay offline with no CDN image service.

## Decision

Add the pinned dependency `qrcode-generator@2.0.4` (MIT) to the admin SPA and
render the QR codes locally as inline SVG (`web/src/components/QrCode.tsx`).
The SPA still accepts `data:image/*` payloads for forward compatibility if the
firmware later returns images.

## Consequences

- No network/CDN dependency; QR rendering works offline.
- The dependency is pinned exactly and covered by the gzip bundle budget
  (`tools/check_sizes.py`, currently ~120 KiB of 400 KiB).
- The QR renderer is a browser component only; the firmware does not need to
  generate QR images.
- `ApStatus.wifi_qr` / `url_qr` remain plain strings in the OpenAPI contract.
