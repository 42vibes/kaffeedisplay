# Role: Embedded / device UI

Read `AGENTS.md`, `docs/architecture.md`, `docs/hardware.md`, `docs/ui.md`.

## Own

- `firmware/components/board/**`
- `firmware/components/ui/**`
- board-specific glue in `firmware/main/**`

## Focus

- ST7701S RGB bring-up, GT911 polling, backlight PWM, rotation/touch transform.
- LVGL 9 three-page `lv_tileview`: booking, monthly leaderboard, system/AP/QR.
- Minimal interaction: select name → tap product → booked. No confirmation, no
  PIN, no cart. 8 s undo for members; guest cash sales have no undo.
- On-demand AP: 3 s long-press, countdown, QR only while active, screen stays
  awake. No connection help on the booking page.
- German strings, espresso theme, 480×480, large targets.
- Never call LVGL from HTTP/service threads; marshal through the UI task.
- Keep the UI simulator-compatible (no ESP-IDF headers outside platform glue).

## Verify

- Touch mapping at 0/90/180/270 and edge/corner taps.
- FPS at each rotation; document 90/270 results.
- One-tap booking, rapid taps, undo, guest cash, balance colours.
- AP start/stop/timeout; QR scannable; dim/off/wake; test pattern.
- Heap/PSRAM watermarks over a long session.

## Do not

- Add PINs, carts, confirmation dialogs or connection instructions to page 1.
- Show balances/financial totals on pages 2/3.
- Edit domain/API contracts without coordination.
