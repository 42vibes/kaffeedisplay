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
03-round-toast.png
04-leaderboard.png
05-system-ap-qr.png
```

These are committed and shown in `README.md`.

## Hardware-in-the-loop

When `/dev/ttyUSB0` is present:

```sh
make flash
make hil
```

`make hil` should:

1. flash a test build,
2. wait for boot,
3. read serial logs,
4. connect to the AP or LAN,
5. run the API smoke tests (login, create temp user/product, purchase, topup,
   void, report CSV, OTA dry-run),
6. factory-reset test data.

CI cannot reach hardware; firmware build is the required CI gate.

## Fault injection

- truncate journal at every byte offset, verify recovery to a committed prefix
- corrupt hash, verify rejection and quarantine
- power-cut during snapshot/compaction
- storage full, network loss, AP timeout with active admin session
- 72 h soak with periodic bookings, admin reads, reports and backups
- rotation/brightness changes under load
