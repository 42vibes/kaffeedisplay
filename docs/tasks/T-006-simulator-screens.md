# T-006 — LVGL simulator + screenshots + README

Status: blocked on T-003
Track: ui
Owner: –
Branch: `feat/T-006-simulator-screens`

## Context

UI agents need a fast feedback loop without the board, and the repo README should
show the actual device UI.

## Deliverables

1. `sim/` CMake project: LVGL 9 + SDL2, reuses `ui/` and `domain/` + a fake
   `IUiBackend` with seeded users/products/events.
2. Mouse = touch; keyboard shortcuts for rotation/brightness; CLI flags:
   `--screenshot <file>`, `--page 1|2|3`, `--rotation 0|90|180|270`.
3. Deterministic screenshot mode (fixed time, seeded data, stable animations).
4. Generate and commit:
   - `docs/ui/screenshots/01-idle.png`
   - `docs/ui/screenshots/02-user-selected.png`
   - `docs/ui/screenshots/03-booking-toast.png`
   - `docs/ui/screenshots/04-leaderboard.png`
   - `docs/ui/screenshots/05-system-ap-qr.png`
5. Embed the screenshots in `README.md` under a “Screenshots” section.
6. `make sim` and a short `sim/README.md`.

## Acceptance

- `make sim` runs on Linux with SDL2, no board required.
- Screenshots are reproducible, committed, and referenced by README.
- The fake backend exercises the same UI paths as firmware (no forked widgets).

## Files

`sim/**`, `docs/ui/screenshots/**`, `README.md`, `Makefile`.

## Evidence

<!-- PR, screenshot diff, run log -->
