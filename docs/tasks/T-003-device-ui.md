# T-003 — Board bring-up + 3-page LVGL device UI

Status: review
Track: embedded
Owner: ui-agent
Branch: `feat/T-004-ui`

## Context

Hardware: Guition ESP32-4848S040, 480×480 ST7701S RGB, GT911, 16 MB/8 MB PSRAM.
Interface: `firmware/components/board/include/kaffee/board.hpp` and
`firmware/components/ui/include/kaffee/ui.hpp`.

## Deliverables

1. Board component:
   - ST7701S RGB init (type-9 sequence), two PSRAM framebuffers, vsync flush.
   - GT911 polling at 100 Hz, touch transform for rotation 0/90/180/270.
   - LEDC backlight GPIO38, 20 kHz, gamma, fades, active/idle/off states.
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
- Host simulator screenshots in `docs/ui/screenshots/`.
- PR: https://github.com/Babo96/kaffeedisplay/pull/8
