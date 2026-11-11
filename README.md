# Kaffeekasse

A self-contained coffee fund kiosk for the **Guition ESP32-4848S040** (ESP32-S3,
4.0" 480×480, ST7701S RGB panel, GT911 touch, 16 MB flash, 8 MB PSRAM).

- **Device:** trust-based, two-tap booking on LVGL 9. Select your name, tap a
  drink, done. Optional guest cash sales.
- **Admin:** on-device web backend over a demand-activated WLAN AP (or the LAN).
  Users, drinks, payments, expenses, reports, CSV exports, backup, OTA, display
  settings.
- **Self-contained:** no cloud, no external database, no runtime internet
  dependency. Optional STA Wi-Fi is only used for time sync and convenience.

## Status

Early development. See `docs/` for architecture, task board and decisions.

## Quick start

```sh
make bootstrap    # check toolchains, install web dependencies
make test         # host unit tests + web unit tests
make web          # build the admin SPA
make build        # build firmware (requires ESP-IDF 5.x or Docker)
make flash        # flash /dev/ttyUSB0
make sim          # LVGL simulator on the host
make check        # everything CI runs
```

The board is normally at `/dev/ttyUSB0` (CH340). Flashing from Docker:

```sh
make flash-docker
```

## Documentation

| Document | Contents |
|---|---|
| `AGENTS.md` | Rules and workflow for human and AI contributors |
| `docs/architecture.md` | Runtime architecture and module boundaries |
| `docs/hardware.md` | Board, pin map, display/touch/power notes |
| `docs/data-model.md` | Domain types and ledger schema |
| `docs/accounting.md` | Post-pay, guest cash sales, cash reconciliation |
| `docs/api/openapi.yaml` | Admin REST contract |
| `docs/ui.md` | Device and admin UI specification |
| `docs/security.md` | Threat model, auth, AP policy |
| `docs/operations.md` | Provisioning, backup, OTA, recovery |
| `docs/testing.md` | Test strategy and hardware-in-the-loop |
| `docs/tasks/` | Task board for parallel agents |

## License

MIT. See `LICENSE`.
