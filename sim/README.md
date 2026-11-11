# LVGL simulator

The simulator builds the real `ui/` and `domain/` code with LVGL 9 + SDL2 and a
fake `IUiBackend`. It is the fastest way to iterate on the device screens and the
source of the README screenshots.

## Planned usage

```sh
make sim
./sim/build/kaffeekasse-sim --page 1
./sim/build/kaffeekasse-sim --page 3 --rotation 90
./sim/build/kaffeekasse-sim --screenshot docs/ui/screenshots/01-idle.png --page 1
```

Mouse click = touch. `1`/`2`/`3` switch pages, `r` cycles rotation, `+`/`-`
changes brightness, `q` quits.

## Determinism

Screenshots are generated with fixed time, seeded users/products/events and
animations disabled, so a rerun produces the same image (except antialiasing
differences across SDL versions).

## Screenshot set

`docs/ui/screenshots/`:

- `01-idle.png` — page 1, no user selected
- `02-user-selected.png` — page 1, member + products
- `03-booking-toast.png` — page 1, booking toast + undo
- `04-leaderboard.png` — page 2, monthly ranking
- `05-system-ap-qr.png` — page 3, AP active with QR codes

These are committed and embedded in the top-level `README.md`. Regenerate and
commit them whenever the UI changes visibly.
