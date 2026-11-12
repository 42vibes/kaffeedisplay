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
make hil                         # read-only: serial + login + GET checks
make hil HIL_ALLOW_WRITES=1 HIL_ADMIN_PASSWORD='...'
# or with a known password / LAN address:
HIL_ALLOW_WRITES=1 HIL_ADMIN_PASSWORD='...' \
  HIL_BASE_URL=http://kaffeekasse.local/api/v1 make hil
```

`make hil`:

1. optionally flashes (`HIL_FLASH=1` / `--flash`),
2. reads the serial console (a fresh `Kaffeekasse booting` banner is only
   expected with `HIL_RESET=1` / `--reset`; otherwise the script accepts a
   read-only `status` reply as proof the app is alive),
3. optionally starts the AP over the serial CLI (`HIL_START_AP=1` /
   `--start-ap`; **off by default**),
4. logs in and executes read-only checks (`/health`, `/users`, `/products`,
   `/stats/summary`, `/time`),
5. only with `HIL_ALLOW_WRITES=1` / `--allow-writes`: creates a temp
   user/product, books through the serial `book` command, tops up, voids via the
   serial `void` command and downloads a report CSV.

Cleanup **voids the test events and archives the temp user/product**; the
append-only ledger keeps the audit trail. Nothing is hard-deleted.

`tools/hil.py` is a pytest module; `python3 -m pytest tools/hil.py -q -s` works
too. It skips with a clear message when the serial port is absent or the HTTP
API is unreachable. It fails — never silently skips — when a connected board does
not run Kaffeekasse firmware.

| Variable | Default | Meaning |
|---|---|---|
| `HIL_PORT` | `/dev/ttyUSB0` | serial device |
| `HIL_BAUD` | `115200` | serial baud rate |
| `HIL_BASE_URL` | `http://192.168.4.1/api/v1` | API base (AP or LAN) |
| `HIL_ADMIN_USER` | `admin` | admin username |
| `HIL_ADMIN_PASSWORD` | – | required for HTTP checks |
| `HIL_ALLOW_WRITES` | `0` | opt in to ledger-mutating smoke tests |
| `HIL_RESET` | `0` | reset the board to capture the boot banner |
| `HIL_START_AP` | `0` | ask the device to start its AP |
| `HIL_FLASH` | `0` | run `make flash` first |
| `HIL_KEEP` | `0` | leave temp data/events for inspection |
| `HIL_TIMEOUT` | `10` | HTTP timeout in seconds |

The serial protocol and the test-only `book`/`void` commands are in
`docs/serial-cli.md`. Install dependencies with `make tools-deps`.

CI cannot reach hardware; firmware build is the required CI gate.

## Web assets

```sh
make web                 # Vite build
make assets              # deterministic gzip/raw embed -> webassets/generated (gitignored)
python3 tools/embed_web.py --check
```

`tools/embed_web.py --check` fails when generated assets are stale. The gzip
budget is enforced by `tools/check_sizes.py`.

Device fonts are committed by the UI track under
`firmware/components/ui/src/fonts/` and do not need a Node toolchain for the
firmware build. See that directory's README for the pinned `lv_font_conv`
regeneration command.

## Fault injection

- truncate journal at every byte offset, verify recovery to a committed prefix
- corrupt hash, verify rejection and quarantine
- power-cut during snapshot/compaction
- storage full, network loss, AP timeout with active admin session
- 72 h soak with periodic bookings, admin reads, reports and backups
- rotation/brightness changes under load
