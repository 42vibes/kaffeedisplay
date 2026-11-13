# T-013 — Animated coffee idle / power-save display

Status: in-progress
Track: embedded UI
Owner: 42vibes
Branch: `feature/animated-coffee-standby`
PR: [Draft #3 on fork](https://github.com/42vibes/kaffeedisplay/pull/3)

## Scope

- Show a tasteful coffee cup with three rising steam strokes on the 480×480 display after the existing administrative dim timeout.
- Draw the cup and animate steam using native LVGL primitives only (no sprite sheet, GIF or compressed full-screen images).
- Reuse `DisplaySettings.dim_timeout_s`, `brightness_idle` and `screen_off_timeout_s` unchanged.
- Stop steam updates after screen-off; preserve a full-screen wake target so an invisible tap never books a product.
- Keep the existing AP stay-awake behavior and avoid touching user/avatar, accounting and REST contracts.

## Acceptance

- Real moving steam appears at idle, unless the admin disabled dimming or the access point is active.
- Animation pauses after screen-off. First tap dismisses standby and never purchases a drink.
- Existing simulator booking, scrolling and AP tests still pass.
- A deterministic screenshot demonstrates the standby view.
- Host, web, simulator, firmware and size checks succeed on the final branch.
- Physical-device idle dim / wake / power use must be reviewed before upstream submission.

## Evidence

- [Standby screenshot](../ui/screenshots/06-coffee-standby.png) generated from the real LVGL simulator.
- [CI for this draft](https://github.com/42vibes/kaffeedisplay/pull/3/checks).
