# ADR 0007 — Admin SPA embedded in firmware

Status: accepted (2026-11-11)

## Context

OTA should update the backend and its UI atomically. The device must work with
no CDN or internet.

## Decision

The Vite build output is embedded into the firmware image (generated C array or
`EMBED_FILES`) during `make build`. No runtime asset partition is updated
separately. All fonts, icons and JS/CSS are bundled locally.

## Consequences

- One OTA image updates firmware + admin UI.
- Firmware size grows by the gzipped SPA (< 400 KB budget).
- `web/dist` stays out of git; CI builds it before the firmware build.
