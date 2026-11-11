# API implementation notes (T-008 / `firmware/components/api`)

Status: routes, auth wiring, static SPA, backup/restore and OTA are implemented
against `docs/api/openapi.yaml` and `service.hpp`. Two integrations are still
owned by other tracks and are explicit placeholders here.

## Layout

| File | Purpose |
|---|---|
| `include/kaffee/api.hpp` | frozen `ApiServer` interface (unchanged) |
| `include/kaffee/api_util.hpp` | pure routing/query/cookie/CSV/time helpers (host-tested) |
| `src/api.cpp` | `ApiServer` lifecycle, single catch-all dispatcher, auth/CSRF middleware, JSON errors, log ring |
| `src/api_handlers.cpp` | REST handlers and the route table |
| `src/api_assets.cpp` | embedded SPA with gzip, SPA fallback and cache policy |
| `src/api_ota.cpp` | streaming multipart OTA with SHA-256 and `esp_ota_*` |
| `src/api_util.cpp` | helper implementation |

The HTTP server is started by `main`/`net` only while an interface is up. One
`httpd` handler (`/*`, `HTTP_ANY`) dispatches internally so path parameters,
method checks (405) and JSON 404s stay consistent.

## Auth

- Session cookie `kaffee_session`: `HttpOnly; SameSite=Strict; Path=/`.
  No `Secure` flag because the device serves plain HTTP by design.
- Every request calls `KaffeeService::validate_session`; there is no parallel
  session store in the API.
- Writes require `X-CSRF-Token`, validated with `KaffeeService::validate_csrf`.
- Login calls `KaffeeService::login(username, password, client_ip)`; rate-limit
  and lockout errors are mapped from the service error code to 429.

## Idempotency and errors

- Ledger writes require a valid `X-Request-Id` (1–64 printable ASCII) and pass
  it to the service. A supplied id is echoed back as a response header.
- Errors are `{"code": "...", "message": "..."}` in German. Service error codes
  are mapped to 400/401/403/404/409/413/429/500/501/503.
- Body limits: 32 KiB for JSON, 4 MiB for `/restore`, ~3 MiB (OTA partition) for
  `/ota`; oversized JSON returns 413 before reading the body.

## Static SPA

- `GET /` and any extension-less path serve `index.html` with `no-cache`.
- `/assets/<name>-<hash>.<ext>` is served `public, max-age=31536000, immutable`.
- `Accept-Encoding: gzip` selects the pre-compressed body when the asset table
  provides one; `ETag`/`If-None-Match` produce 304.
- No filesystem dependency.

## Integration TODOs

1. **Reports (T-005).** `GET /reports/{kind}` validates the kind and returns
   `501 {"code":"not_implemented"}`. Replace `handle_reports` in
   `src/api_handlers.cpp` with a call into `kaffee/reports.hpp` once that
   component lands; stream CSV with `send_chunked_text`/`httpd_resp_send_chunk`
   and set `Content-Disposition` via `api::content_disposition`.
2. **Embedded web assets (T-007).** `src/api_assets.cpp` serves a German
   fallback page. When the generated asset table exists, add the component to
   `api/CMakeLists.txt`, define `KAFFEE_API_WEB_ASSETS` and implement
   `kaffee_web_asset_lookup(std::string_view)` (see the commented block in
   `api/CMakeLists.txt`). The only adapter is `find_asset`.
3. **Main wiring.** `main.cpp` still boots the skeleton. It must create the
   service/platform, call `ApiServer::create(service, platform)`, and
   start/stop the server from the net interface callbacks (AP up/down, STA
   connect/disconnect). Sessions must be cleared when the AP stops
   (`KaffeeService::logout`/net callback).
4. **Net callbacks.** Captive-portal probe paths (`/generate_204`,
   `/hotspot-detect.html`, `/connecttest.txt`, `/ncsi.txt`) are served by `net`,
   not by this component; the API returns JSON 404 for unknown `/api/...`.
5. **OTA rollback.** The image is written and the boot partition switched.
   `esp_ota_mark_app_valid_cancel_rollback()` after a healthy boot belongs to
   `main` (already present in the skeleton).
6. **Log ring.** `GET /logs` reads a 48-line in-RAM ring installed while the
   server runs. A persistent ring buffer is out of scope for v1.

## Contract gaps found (OpenAPI not changed)

The implementation follows the spec, but these write routes do not declare
`X-Request-Id` even though they mutate state. The API accepts the header when
present and generates an internal id otherwise, so idempotency is weaker there:

- `POST /time/sync`
- `POST /settings/password`
- `POST /ap/stop`, `POST /ap/extend`
- `POST /display/test`
- `POST /restore`
- `POST /ota`

Other observations:

- `Event.voided` and `Event.balance_after_cents` are derived by the API from the
  ledger (void events and a single prefix pass); the ledger stores neither.
- `GET /users/{id}/transactions?before=` is implemented as an exclusive event
  `seq` cursor because the spec does not define the unit.
- `GET /users` has no `include_archived` parameter and returns all users,
  including archived ones with `active: false`.
- `GET /stats/leaderboard` only accepts `month`; a missing month uses the
  device clock and falls back to the last 30 days when time is unset.
- `/device` and `/summary` return a few additional diagnostic fields
  (`min_free_heap`, `storage_mounted`, `ap_starting`, …); the spec allows
  additional properties.
- `POST /ap/start` while the AP is already active returns the service error
  (409 for `ap_already_active`).
