# HTTP API implementation notes (T-008, part of T-004)

Status: all contract routes are implemented in `firmware/components/api` against
`docs/api/openapi.yaml`, `service.hpp` and the generated `kaffee/web_assets.hpp`.
T-009 wires the API into the firmware boot path: reports, factory reset,
embedded SPA assets, network-driven start/stop and settings/time application.

T-004 is split into two branches:

| Part | Branch | Scope |
|---|---|---|
| API | `feat/T-008-api` (PR #4) | `firmware/components/api`, host contract tests |
| Net | `feat/T-005-api-net` | `firmware/components/net`, lifecycle and captive DNS |

## Layout

| File | Purpose |
|---|---|
| `include/kaffee/api.hpp` | frozen `ApiServer` interface (unchanged) |
| `include/kaffee/api_util.hpp` | routing/query/cookie/CSV/time/error/static helpers (host-tested) |
| `include/kaffee/api_routes.hpp` | route metadata: method, auth policy, request-id requirement (host-tested) |
| `include/kaffee/api_multipart.hpp` | boundary parser and streaming multipart scanner (host-tested) |
| `src/api.cpp` | lifecycle, catch-all dispatcher, auth/CSRF middleware, JSON errors, captive probes, log ring |
| `src/api_handlers.cpp` | REST handlers + handler bindings in `route_specs()` order |
| `src/api_assets.cpp` | `webassets` table, fallback page, gzip, SPA fallback, cache policy |
| `src/api_ota.cpp` | streaming multipart OTA, SHA-256, `esp_ota_*`, reboot |

One `httpd` handler (`/*`, `HTTP_ANY`) dispatches internally. `route_specs()` and
`route_handlers()` are parallel arrays and the dispatcher rejects a length
mismatch instead of mis-routing.

## Auth, sessions, CSRF

- Session cookie `kaffee_session`: `HttpOnly; SameSite=Strict; Path=/`. No
  `Secure` flag because the device serves plain HTTP by design. `httpd_resp_set_hdr`
  stores the value pointer rather than copying it, so the cookie header is
  formatted into a server-lifetime buffer. When the wall clock is unset,
  `expires_at` is 0 and the cookie uses a 30-minute fallback `Max-Age`; service
  session expiry still uses monotonic uptime.
- Every request calls `KaffeeService::validate_session`; there is no parallel
  session store. `GET /auth/me` re-issues the cookie with the refreshed sliding
  expiry.
- Writes require `X-CSRF-Token`, validated with `KaffeeService::validate_csrf`.
- While `KaffeeService::must_change_admin_password()` is true, only
  `POST /auth/logout`, `GET /auth/me`, `GET /settings` and
  `POST /settings/password` are allowed. Everything else returns
  `403 password_change_required` so the SPA can force the first-login change.
  `GET /auth/me` and `POST /auth/login` include `must_change_password` so the
  SPA can route to the forced change form before rendering the shell.
- The generated initial password is persisted in NVS (dedicated key, not in
  `Settings`, backups or the journal) and shown on device page 3 until the
  change succeeds. It survives reboots.
- A successful password change calls `logout()` for the current token and clears
  the cookie. **TODO(T-001):** `change_admin_password()` must clear all sessions
  in the service; the API cannot enumerate other tokens without a parallel
  store.
- Login passes the client IP to the service; `auth` maps to 401 and
  `rate_limit` to 429.
- PBKDF2 uses mbedTLS's SHA-accelerated implementation on the device (10 000
  iterations, ~0.9 s login). Older 50 000-iteration hashes are rehashed
  transparently on the next successful login. Host tests keep the portable
  implementation and standard PBKDF2 vectors.

## Errors

Service/ledger errors are mapped by `map_service_error()`:

| Service code | Status | Notes |
|---|---|---|
| `auth`, `session` | 401 | wrong credentials / expired session |
| `rate_limit` | 429 | login backoff |
| `password`, `settings`, `backup`, `amount`, `price`, `time`, `ap`, `name`, `type`, `seq`, `id` | 400 | validation |
| `void` | 404 for "not found"/"unknown", else 409 | double void is 409 |
| `event` | 404 | unknown booking |
| `expense`, `user`, `product` | 404 for unknown, 409 for already/archived, else 400 | |
| `debt`, `duplicate` | 409 | credit limit / replay |
| `chain`, `hash`, unknown code | 500 | never exposes internals |

The response keeps the stable service code and adds a German `message`. English
service/ledger messages are never forwarded; the SPA may translate the code
instead. API-level validation errors use German messages directly.

## Idempotency, limits, caching

- Ledger writes require a valid `X-Request-Id` (1–64 printable ASCII) and pass it
  to the service; it is echoed as a response header. `/restore` and
  `/settings/password` also require it (see contract gaps). No random id is
  generated for retries.
- Body limits: 32 KiB JSON, 4 MiB restore, ~3 MiB OTA (partition size).
- JSON bodies must be objects; `nlohmann::json` type errors in settings become
  400 instead of 500.
- JSON/backup responses use `Cache-Control: no-store`.

## Settings

`PUT /settings` applies the provided subtrees and keeps omitted values, matching
the all-optional `Settings` schema. Empty `ap_password`/`sta_password` keep the
stored secret. Validation covers display ranges, AP window, hostname, SSID
length, WPA2 password length (8–63 when set), timezone/SNTP lengths, currency,
locale and a non-negative `max_open_debt_cents`. The merge-vs-replace choice is
documented here because the spec only says "replace settings".

## Reports

`GET /reports/{kind}` now uses `kaffee::reports`:

- `period=week|month|year|all` is computed on the device in `Europe/Berlin`;
  `period=custom` requires `from`/`to` (Unix seconds, `to` exclusive).
- `format=json` returns the envelope from `docs/api/reports.md`
  (`kind`, `period`, `from`, `to`, `columns`, `rows`, `time_quality`). Money
  cells are integer cents.
- `format=csv_excel` and `format=csv_raw` build the CSV through the shared
  `reports::write_csv` writer, then send it in `httpd_resp_send_chunk` slices.
  Reports are kiosk-sized, so one request buffer avoids holding the service
  mutex across socket writes.
- `Accept: text/csv` selects Excel when `format` is omitted.
- If the device clock is unset, bounded periods return `409 time_unset` instead
  of silently reporting 1970; `period=all` still works.
- For `period=all`, the JSON envelope omits `from`/`to` (unbounded). Bounded
  periods include `from`/`to`.
- The API takes a `KaffeeService::ledger_snapshot()` copy for the request,
  matching the snapshot model from ADR 0012.

## Factory reset

`POST /factory-reset` requires a session, CSRF token and `X-Request-Id`. It
returns `204` immediately and a helper task then erases the event journal,
snapshots and settings, resets the network credentials and reboots. The next
boot goes through the first-boot path and generates a new setup password.

## Server lifecycle

- `main` creates `ApiServer` once and starts/stops it from `ApiController`, a
  worker task fed by `INetworkListener`. The listener callbacks only enqueue; the
  worker calls `start()`/`stop()` with a short grace delay so the response that
  changed the network state can flush.
- `on_ap_stopped()` runs `IPlatform::notify_ap_stopped()`, which invokes the
  service callback that clears admin sessions, and then stops the server if no
  STA interface remains.
- `ApiServer::stop()` still has its internal deferred-stop fallback for any
  future call made from the httpd task. The destructor waits briefly for a
  pending deferred stop.
- HTTP receive/send timeouts are 10 s so a stalled OTA upload cannot pin a
  socket indefinitely.

## Settings and time

`KaffeeService::save_settings()` applies display settings and now also forwards
the merged (secret-bearing) `NetworkSettings`/`TimeSettings` to the platform
hooks added in ADR 0013. `POST /time/sync` reaches `NetworkManager::sync_time()`
through `IPlatform::sync_time()`.

## Static SPA, webassets and captive probes

- `GET /` and extension-less paths serve `index.html` with `no-cache`.
- `/assets/<name>-<hash>.<ext>` uses `public, max-age=31536000, immutable`;
  other files 1 hour. `ETag`/`If-None-Match` produce 304.
- Gzip is used when the asset table has a pre-compressed body and the client
  accepts it.
- **HEAD is not served** for static files (405): `esp_http_server` owns
  `Content-Length`, so a manual HEAD response would be ambiguous. The policy is
  unit-tested.
- Captive probes: `GET /generate_204` returns 204; `/hotspot-detect.html`,
  `/connecttest.txt` and `/ncsi.txt` return 302 to `http://<ap-ip>/` while the AP
  is active and 404 otherwise. The net component owns the AP lifecycle; the API
  only reflects it.
- Assets come from `kaffee/web_assets.hpp` (`kaffee::api::Asset`,
  `kaffee_web_asset_lookup`). The `webassets` component is in the API `REQUIRES`;
  `make build` runs `make assets` first. Without generated assets (clean clone,
  no `make assets`) a small German fallback page is served. T-009 keeps
  `kaffee::web::assets()` referenced from `main.cpp` so the table cannot be
  dropped by the linker.

## OTA

- Streaming multipart parser: boundary/`name="firmware"` parsing and the
  chunk-split scanner are pure helpers with host tests. A trailing `--` closing
  boundary is required.
- Incremental SHA-256 is computed while writing; `X-Firmware-SHA256` is verified
  when supplied. `esp_ota_end` validates the image, then the boot partition is
  switched and the device reboots after the response is sent.
- `main.cpp` keeps rollback armed until `boot_health_check()` passes. The check
  covers NVS, LittleFS mount, service creation, display/LVGL, the three-page UI
  and network init. Only then is `esp_ota_mark_app_valid_cancel_rollback()` called.

## Remaining integration notes

1. **Serial CLI.** `docs/serial-cli.md` documents the UART0 console
   (`status`, `time set`, `setup-password`, `book`, `void`, `factory-reset`).
   T-010 implements the bounded line reader in `firmware/main/serial_console.*`;
   `book`/`void` are locked until `unlock-writes` or a HIL build. HIL write
   tests now unlock automatically. A known `ap stop`/synchronous AP-stop hang is
   tracked in the T-010 task file; factory reset erases the network NVS
   namespace directly and reboots.
2. **Service session revocation.** `change_admin_password()` should clear all
   sessions (T-001). The API revokes the current one and documents the gap.
3. **Captive DNS on the device.** The net component answers probe hosts; the API
   handles the probe paths once the request reaches port 80. This was verified
   by code inspection and net host tests, not by a phone captive-portal flow.

## Contract gaps (OpenAPI not changed)

These mutating routes do not declare `X-Request-Id`; the API requires it anyway
to avoid duplicate writes on retries. The spec should add the parameter:

- `POST /settings/password`
- `POST /restore`

The following are weaker idempotency (optional header, no generated id):

- `POST /time/sync`, `POST /ap/stop`, `POST /ap/extend`, `POST /display/test`,
  `POST /ota`

Other observations:

- `Event.voided`/`Event.balance_after_cents` are derived by the API; the ledger
  stores neither.
- `GET /users/{id}/transactions?before=` is an exclusive event `seq` cursor.
- `GET /users` returns archived users too (`active: false`); the spec has no
  `include_archived` parameter.
- `GET /stats/leaderboard` only accepts `month`; with time unset it falls back to
  the last 30 days.
- `/device` and `/summary` return a few extra diagnostics (allowed by the spec).
- `GET /settings` returns the stored AP SSID override; when empty the device
  derives `Kaffeekasse-XXXX` from the MAC. The effective SSID, AP password and
  URL are available from `GET /ap` and `GET /device.network` while the AP is
  active.
- The login response carries the session only in the cookie; the body contains
  username, CSRF token and expiry.
