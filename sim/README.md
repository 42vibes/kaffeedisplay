# LVGL simulator

The simulator builds the real `ui/` and `domain/` code with LVGL 9.6 and SDL2
plus a fake `IUiBackend`. It is the fastest way to iterate on the device screens
and the source of the README screenshots.

## Requirements

- Linux with SDL2 headers (`libsdl2-dev`), zlib and CMake ≥ 3.20.
- Network access on the first configure: CMake fetches the pinned LVGL tag
  `v9.6.0` (the same version as `firmware/main/idf_component.yml`).

## Build and run

```sh
make sim
# or
cmake -S sim -B sim/build -DCMAKE_BUILD_TYPE=Release
cmake --build sim/build -j
./sim/build/kaffeekasse-sim
```

Mouse click = touch. Keys:

| Key | Action |
|---|---|
| `1` / `2` / `3` | Buchen / Statistik / System |
| `r` | cycle rotation 0 → 90 → 180 → 270 |
| `+` / `-` | brightness up/down |
| `q` / `Esc` | quit |

The simulator draws the display itself (no `esp_lvgl_port`) and applies rotation
in its flush callback, mirroring what the board display port will do.

## Screenshot mode

```sh
./sim/build/kaffeekasse-sim --screenshot out.png --page 1
./sim/build/kaffeekasse-sim --screenshot out.png --page 1 --state selected
./sim/build/kaffeekasse-sim --screenshot out.png --page 1 --state toast
./sim/build/kaffeekasse-sim --screenshot out.png --page 3 --state ap
./sim/build/kaffeekasse-sim --screenshot out.png --page 1 --rotation 90
```

`--state` creates a deterministic UI state through the same code paths as touch
input:

- `idle` — page 1 with user tiles, no selection
- `selected` — first seeded member selected
- `toast` — first member books the first product; 8 s undo toast visible
- `ap` — WLAN hotspot active with Wi-Fi join and admin URL QR codes

Screenshot mode forces the SDL dummy video driver, a fixed wall clock
(2026-11-11 09:41 Europe/Berlin) and a fixed 16 ms tick, so reruns produce the
same pixels (modulo SDL/zlib versions).

## Regenerate the committed set

```sh
make screenshots
```

This writes `docs/ui/screenshots/01-idle.png` … `05-system-ap-qr.png`. Commit
the result whenever the UI changes visibly.

## Fake backend

`src/fake_backend.cpp` seeds five members, six drinks, top-ups, purchases and
guest sales through the real `kaffee::Ledger`. Balances, leaderboards, voids and
undo therefore use production domain code. AP/time/device stats are simulated.
`firmware/components/ui/include/kaffee/ui_display.hpp` is the display port the
board component can implement later; `ui_control.hpp` is the simulator-only page
and state control surface.
