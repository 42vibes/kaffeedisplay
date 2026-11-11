# ADR 0009 — Display rotation and two brightness states

Status: accepted (2026-11-11)

## Context

The device may be mounted in different orientations. The backlight is the main
power/user-comfort control and should not be a single hardcoded value.

## Decision

Admin-configurable rotation (0/90/180/270) via LVGL + touch transform, applied
live with revert-on-failure. Two on states: active brightness and idle/dim
brightness, plus an optional screen-off timeout. LEDC PWM at ~20 kHz with gamma
correction and short fades. The QR screen stays at active brightness while the
AP is active.

## Consequences

- 90/270 may cost fps due to software rotation; benchmark and optimize.
- If rotation breaks rendering, the admin can revert from the web.
- Settings live in NVS and are applied by the board component.
