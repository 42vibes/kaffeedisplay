# ADR 0015: Native LVGL standby coffee animation

Status: Proposed

## Context

The Guition ESP32-S3 kiosk already supports administrative dim and screen-off
timers. We want a recognisable, gentle, coffee-themed standby view without
exhausting the limited 3 MiB OTA application partition and without introducing
a large full-screen GIF or a new image decoder. The separate illustrated-avatar
PR must remain unchanged.

## Decision

- Keep the existing persisted `DisplaySettings` unchanged and let the board
  control physical backlight power. The UI observes LVGL input inactivity and
  shows an overlay only after the configured dim or off threshold.
- Draw a filigree, outlined porcelain cup and rising steam with native LVGL
  lines. Curves are pre-sampled from cubic Bézier geometry into immutable
  point arrays at source-generation time; no per-frame Bézier computation,
  heavy vector rendering or thick polygonal outlines. Animate three line
  objects from
  one `80 ms` LVGL timer; the three cycles are staggered by 3.2 seconds,
  each rising roughly 56 pixels and fading in/out independently. No image
  frames, filesystem assets, network connection or allocations per frame.
- Pause the animation after the configured screen-off threshold, without
  removing the full-screen overlay. The next touch is intercepted and wakes
  the screen instead of triggering a blind booking. The overlay disappears
  on activity.
- Disable the standby view whenever the admin AP is open or the rotation/test
  overlay is active; do not change finance, ledger or REST interfaces.
- Provide deterministic simulator screenshot evidence and explicit movement,
  wake and animation-pause checks.

## Consequences and limitations

- The user cannot interact with the booking page through the standby overlay
  until the first wake tap is received. This is intentional.
- Animations are drawn only during the dimmed interval, not when the backlight
  is off. This reduces unnecessary rendering and power consumption, but is not
  deep sleep: CPU and display hardware remain controlled by existing firmware.
- Board and LVGL input activity share the same physical touch source; a real
  ESP32 hardware soak test is required for timing/energy validation.
- No new dependency, file format or settings migration is required.

## Alternatives

A GIF or frame-by-frame RGB565 animation would consume significantly more
flash/PSRAM and require decoding or large embedded resources. Native LVGL
primitives are simpler and have a smaller OTA footprint.
