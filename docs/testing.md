# Testing

## Layers

| Layer | Tool | Runs |
|---|---|---|
| Domain/storage unit + property tests | doctest, ASan/UBSan | host, every PR |
| API contract | OpenAPI + mock/device | host/CI |
| Admin web unit | Vitest | CI |
| Admin web e2e | Playwright vs mock | CI |
| Device UI layout | LVGL simulator + screenshots | local/CI optional |
| Hardware-in-the-loop | pytest + serial + HTTP | local with board |
| Soak/fault injection | host + device scripts | pre-release |

## Host tests

```sh
make test-host
```

Cover:

- balances, post-pay negative balances, guest cash, topups, adjustments
- voids (single, double, expense), idempotency, replay
- product deletion snapshots, user archiving
- leaderboard month boundaries, voids, guest separation
- hash-chain corruption and journal truncation at every byte
- snapshot/compaction/recovery
- settings JSON, secrets excluded
- report aggregation and CSV golden files (BOM, delimiter, decimals, injection)

## Web tests

```sh
npm --prefix web test
npm --prefix web run typecheck
npm --prefix web run build
```

Required flows for e2e:

1. Login, wrong password, logout.
2. Create user, topup, see balance and history.
3. Create product, device mock shows it, delete hides it while history remains.
4. Add/edit/void expense; cash updates.
5. Reports week/month/year JSON + CSV download.
6. Display settings rotation/brightness; AP start/extend/stop mock.
7. Time sync from browser; untrusted warning.

## Simulator

`make sim` builds the LVGL SDL app with the real UI + domain and a fake backend.
It must generate deterministic screenshots into `docs/ui/screenshots/`:

```
01-idle.png
02-user-selected.png
03-booking-toast.png
04-leaderboard.png
05-system-ap-qr.png
```

These are committed and shown in `README.md`.

## Hardware-in-the-loop

When `/dev/ttyUSB0` is present:

```sh
make flash
make hil
# or with a known password / LAN address:
HIL_ADMIN_PASSWORD='...' HIL_BASE_URL=http://kaffeekasse.local/api/v1 make hil
```

`make hil`:

1. optionally flashes (`HIL_FLASH=1` / `--flash`),
2. waits for the `Kaffeekasse booting` banner over serial,
3. starts the AP over the serial CLI unless `HIL_START_AP=0`,
4. runs the API smoke test (login, create temp user/product, purchase, topup,
   void, report CSV download),
5. cleans up the temp user/product and the test events.

`tools/hil.py` is a pytest module; `python3 -m pytest tools/hil.py -q -s` works
too. It skips with a clear message when the serial port is absent or the HTTP
API is unreachable. It fails — never silently skips — when a connected board
does not run Kaffeekasse firmware.

| Variable | Default | Meaning |
|---|---|---|
| `HIL_PORT` | `/dev/ttyUSB0` | serial device |
| `HIL_BAUD` | `115200` | serial baud rate |
| `HIL_BASE_URL` | `http://192.168.4.1/api/v1` | API base (AP or LAN) |
| `HIL_ADMIN_USER` | `admin` | admin username |
| `HIL_ADMIN_PASSWORD` | – | required for HTTP checks |
| `HIL_START_AP` | `1` | ask the device to start its AP |
| `HIL_FLASH` | `0` | run `make flash` first |
| `HIL_KEEP` | `0` | leave temp data for inspection |
| `HIL_TIMEOUT` | `10` | HTTP timeout in seconds |

The serial protocol and the test-only `book` command are in
`docs/serial-cli.md`. Install dependencies with `make tools-deps`.

CI cannot reach hardware; firmware build is the required CI gate.

## Assets and fonts

```sh
make web                 # Vite build
make assets              # deterministic gzip embed -> webassets/generated (gitignored)
python3 tools/embed_web.py --check
make fonts               # pinned lv_font_conv@1.5.3 -> ui/generated (gitignored)
```

`tools/embed_web.py --check` fails when generated assets are stale. The gzip
budget is enforced by `tools/check_sizes.py`. Font sources are pinned by URL and
SHA-256; use `KAFFEE_FONT_DIR=...` and `--offline` for reproducible builds
without network access.

## Fault injection

- truncate journal at every byte offset, verify recovery to a committed prefix
- corrupt hash, verify rejection and quarantine
- power-cut during snapshot/compaction
- storage full, network loss, AP timeout with active admin session
- 72 h soak with periodic bookings, admin reads, reports and backups
- rotation/brightness changes under load
