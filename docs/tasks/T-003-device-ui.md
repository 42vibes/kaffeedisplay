# T-003 — Board bring-up + 3-page LVGL device UI

Status: review (board half complete; UI tracked in T-004)
Track: embedded
Owner: board half: T-003-board agent; UI: T-004-ui
Branch: `feat/T-003-board` (board), `feat/T-004-ui` (UI)
PR: #9 (board)

## Context

Hardware: Guition ESP32-4848S040, 480×480 ST7701S RGB, GT911, 16 MB/8 MB PSRAM.
Interface: `firmware/components/board/include/kaffee/board.hpp` and
`firmware/components/ui/include/kaffee/ui.hpp`.

## Deliverables

1. Board component:
   - ST7701S RGB init (type-9 sequence), one PSRAM scanout framebuffer,
     LVGL double-buffered drawing (tear-free scanout double buffering is not
     provided by the current esp_lvgl_port integration).
   - GT911 polling at 100 Hz, touch transform for rotation 0/90/180/270.
   - LEDC backlight GPIO38, 19 kHz/12-bit (20 kHz/12-bit is not reachable on
     the ESP32-S3 APB clock), gamma, fades, active/idle/off states.
   - Rotation/brightness live apply with revert on failure; test pattern.
   - Chip temperature sensor, uptime, heap/PSRAM stats.
2. UI component (`lv_tileview`, 3 pages):
   - Page 1: user select, balance, immediate product booking, 8 s undo toast,
     250–300 ms debounce, 30 s auto-deselect, guest tile, empty state.
   - Page 2: monthly leaderboard by count (no money), prev/next month, guest
     count separate, time-quality warning.
   - Page 3: WLAN start (3 s long-press), countdown/extend/stop, QR only while
     AP active (Wi-Fi join + URL), STA URL QR, time status, device stats,
     read-only display settings.
   - German strings in `ui/src/strings.hpp`, espresso theme, page dots.
3. Refresh on service change callback (queue to LVGL task); no LVGL calls from
   HTTP/service threads.
4. Simulator-compatible: no ESP-IDF headers outside `board`/`ui` platform glue.

## Acceptance

- Device boots to page 1, all three pages swipe, touch mapping correct at all
  rotations.
- One tap books immediately; balance updates; undo reverses; guest sale is cash.
- AP start/stop/QR works; screen stays awake while AP active.
- 30+ fps at 0°, measured fps documented for 90/270.
- Firmware builds with ESP-IDF 5.5; no warnings that indicate memory bugs.

## Files

`firmware/components/board/**`, `firmware/components/ui/**`,
`docs/hardware.md`, `docs/ui.md`.

## Evidence

Board half:

- PR #9: https://github.com/Babo96/kaffeedisplay/pull/9
- Clean `idf.py build` with `espressif/idf:release-v5.5`; host tests include
  touch-transform forward/inverse checks.
- Serial boot log from the flashed unit: LEDC backlight at 19011 Hz/12-bit,
  ST7701S type-9 init, GT911 at 0x5D, temperature 35.2 °C, LVGL port ready,
  and `app_main` returned with the board tasks still running.
- Physical touch-corner taps at 0/90/180/270, visual test-pattern checks and
  fps at 0°/90° still require a person at the panel. The board half is
  otherwise complete.

## Handoff note (board half)

- `IDisplay` is implemented by `EspDisplay` in
  `firmware/components/board/`; UI integration should keep the LVGL calls on
  the LVGL task.
- Scanout uses one PSRAM framebuffer (`num_fbs=1`); LVGL rendering is double
  buffered, but tear-free scanout double buffering is not provided by
  `esp_lvgl_port` 2.9.0 in this configuration.
- Backlight is 19 kHz/12-bit because 20 kHz/12-bit needs a divider <1 on the
  80 MHz APB clock. A boot-time frequency check fails board init if the timer
  cannot be configured.
- GT911 recovery re-creates the driver (no reset/INT wired), and the touch
  transform maps raw panel coordinates for all four rotations.
- AP/QR stay-awake: call `EspDisplay::set_stay_awake(true)` when the AP starts
  and `false` when it stops; this is the board integration point.
- Remaining hardware checks: corner taps at all four rotations, visual test
  pattern, fps at 0°/90°, dim/off/wake timing.
