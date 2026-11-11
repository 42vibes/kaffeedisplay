# Architecture

## Runtime

```
┌──────────────────────── ESP32-S3 · 16 MB flash · 8 MB PSRAM ────────────────────────┐
│  LVGL 9 UI (3 swipe pages) ──┐                                                       │
│  HTTP API (esp_http_server) ──┼──> KaffeeService ──> Ledger (pure C++)               │
│  Wi-Fi AP/STA + DNS/mDNS ─────┘         │              │                             │
│  SNTP / browser time ───────────────────┘              └──> IEventStore (LittleFS)   │
│  Display / touch / backlight (esp_lcd + GT911)                                       │
└──────────────────────────────────────────────────────────────────────────────────────┘
        ▲ touch                                      ▲ HTTP
   user at the machine                        admin phone / laptop
```

- One process, one shared ledger. Device UI calls `KaffeeService` directly.
- Admin web calls the REST API; both paths serialize through the same service
  mutex and the same ledger.
- No cloud, no CDN, no runtime internet dependency. Optional STA Wi-Fi is used
  for SNTP and LAN access.

## Components

| Component | Responsibility | Must not |
|---|---|---|
| `domain` | Types, ledger rules, balances, reports primitives, hash chain, SHA-256, JSON codecs | depend on ESP-IDF, LVGL or filesystem |
| `storage` | LittleFS journal + A/B snapshots, NVS settings, recovery, compaction | contain business rules |
| `service` | `KaffeeService`: mutex, idempotency, time source, sessions, settings, backup, event fan-out | render UI or parse HTTP |
| `board` | ST7701S RGB init, GT911 polling, LEDC backlight, rotation/touch transform, chip temperature | know about users or money |
| `ui` | LVGL pages, theme, German strings, timeouts, test pattern | write storage or call the network directly |
| `net` | AP on demand, STA, DHCP, captive DNS, mDNS, SNTP, time service | serve HTTP routes |
| `api` | REST, auth, CSRF, static SPA, CSV, backup, OTA, diagnostics | mutate the ledger except via service |
| `main` | Boot order, wiring, watchdog, OTA validation | contain feature logic |

## Boot sequence

1. NVS init, mount LittleFS storage partition.
2. Load newest valid snapshot and replay the journal (hash chain verified).
3. Load settings from NVS; generate AP credentials/admin password on first boot.
4. Init board: ST7701S, GT911, backlight, rotation, brightness.
5. Create `KaffeeService` with storage + platform adapter.
6. Init LVGL/UI, show splash then page 1.
7. Init network: STA (if configured) and on-demand AP state machine.
8. Start HTTP server when any interface is up; start mDNS.
9. Mark the OTA image valid after a successful boot.
10. Start time service (restore last known time, mark quality).

## Threading

- LVGL task owns all LVGL calls (via `esp_lvgl_port` locking).
- HTTP server task handles requests; it calls `KaffeeService` and never touches LVGL.
- Service mutex serializes ledger mutations and settings access.
- Storage writes happen under the service mutex; fsync completes before success is
  returned.
- Ledger changes notify the UI through a callback/queue; the UI refreshes on the
  LVGL task, never from the HTTP task.
- Network manager owns AP timers, DNS and SNTP tasks.

## Persistence model

- Append-only `journal.jsonl` + `snapshot.a.json` / `snapshot.b.json`.
- Every event has a sequence number, valid JSON, a SHA-256 hash and the previous
  hash. Boot stops at the first invalid line and quarantines the tail.
- Snapshot stores the full event list up to `last_seq`; compaction is periodic.
- No balance is stored as truth; all balances derive from events.
- Secrets (Wi-Fi passwords, admin hash/salt) live in NVS and are excluded from
  backups.

## Network policy

- AP is **off by default**. It starts only from device page 3 (3 s long-press)
  or explicitly over the LAN API, for a bounded window.
- While any interface is up, the HTTP server and mDNS run.
- Captive DNS/portal run only while the AP is active and are best-effort; the
  Wi-Fi/URL QR codes are the primary connection path.
- Sessions are cleared when the AP stops.

## Time

- Primary source: admin browser `POST /time/sync` on login and periodically.
- Secondary: SNTP when STA has internet.
- Time quality (`synced`, `restored`, `unset`) is recorded per event and shown in
  reports; device page 3 shows source and last sync.
- No battery-backed RTC in v1. Power loss without admin connection yields
  estimated timestamps until the next sync.

## Deployment

- Dual OTA partitions with rollback + health check.
- Admin SPA is embedded in the firmware image for atomic OTA.
- USB (`/dev/ttyUSB0`, CH340) for development, logs and recovery only.
