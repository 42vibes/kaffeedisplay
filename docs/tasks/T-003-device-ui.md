# T-003 — Board bring-up + 3-page LVGL device UI

Status: review (board + UI merged; integration and hardware verification pending)
Track: embedded
Owner: board-agent / ui-agent
Branches: `feat/T-003-board` (board), `feat/T-004-ui` (UI)
PRs: #9 (board), #8 (UI)

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

- LVGL 9.6 device UI (3 pages, German, espresso theme) in `firmware/components/ui/`.
- Host simulator screenshots in `docs/ui/screenshots/`; `make screenshots-check`
  is deterministic under `TZ=UTC`.
- UI click/hit-test smoke test in `sim/` (`make test-ui`).
- PR #8: https://github.com/Babo96/kaffeedisplay/pull/8
- PR #9: https://github.com/Babo96/kaffeedisplay/pull/9
- Clean `idf.py build` with `espressif/idf:release-v5.5`; host tests include
  touch-transform forward/inverse checks.
- Board scanout: first flash showed a blue field with a narrow colour band. Root
  cause was the generic ST7701 init table, not the Guition type-9 sequence.
  Replaced with Arduino_GFX `st7701_type9_init_operations` (including `0xCD 0x00`
  and `0x3A 0x60`) and switched to 12 MHz / hsync 8-20-10 / vsync 8-10-10.
- Diagnostic build (`CONFIG_BOARD_DISPLAY_SELFTEST=y`) produced red/green/blue/
  white flashes and eight vertical colour bars **without scanline artefacts**,
  confirmed visually by the owner. Normal build has selftest off.
- Physical touch-corner taps at 0/90/180/270, visual test-pattern checks and
  fps at 0°/90° still require a person at the panel. Main/UI integration is
  tracked under T-009.

## Handoff note (board half)

- `IDisplay` is implemented by `EspDisplay` in `firmware/components/board/`.
- Scanout uses one PSRAM framebuffer (`num_fbs=1`); LVGL rendering is double
  buffered, but tear-free scanout double buffering is not provided by
  `esp_lvgl_port` 2.9.0 in this configuration.
- Backlight is 19 kHz/12-bit; 20 kHz/12-bit is not reachable on the S3 APB clock.
- GT911 recovery re-creates the driver (no reset/INT wired); touch transform
  maps raw panel coordinates for all four rotations.
- AP/QR stay-awake: call `EspDisplay::set_stay_awake(true)` while AP is active.
- Remaining hardware checks: corner taps at all four rotations, visual test
  pattern, fps at 0°/90°, dim/off/wake timing.
