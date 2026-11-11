# T-004 — HTTP API, auth, AP/time, backup/OTA

Status: blocked on T-001 (service implementation)
Track: backend
Owner: –
Branch: `feat/T-004-api-net`

## Context

Implement `firmware/components/api` and `firmware/components/net` against
`docs/api/openapi.yaml`, `service.hpp` and `net.hpp`.

## Deliverables

1. `NetworkManager`:
   - STA connect/reconnect, AP on-demand with 15 min window, extend/stop,
     countdown, session cleanup on stop.
   - DHCP, captive DNS (A → 192.168.4.1), probe endpoints (`/generate_204`,
     `/hotspot-detect.html`, `/connecttest.txt`, `/ncsi.txt`), mDNS.
   - SNTP optional, browser time sync, time quality persistence in NVS.
   - HTTP server start/stop tied to interface state.
2. `ApiServer`:
   - All routes in OpenAPI: auth/session/CSRF/rate limit, users, products,
     transactions/void, expenses, cash counts, summary, leaderboard, settings,
     time, AP, device, display test, logs, backup/restore, OTA upload.
   - Static SPA from embedded assets, gzip, SPA fallback, no caching for
     index.html.
   - CSV/report hooks (T-005 provides generators).
   - Idempotency via `X-Request-Id`; JSON error shape.
3. OTA: multipart upload, SHA-256, `esp_ota_*`, rollback mark on healthy boot.
4. Auth: PBKDF2, sessions, CSRF, rate limiting, password change; single admin.

## Acceptance

- API contract tests against the mock/device for every route.
- Unauthenticated/CSRF/rate-limit cases return correct status.
- AP is off by default; starts/stops cleanly; QR payloads correct.
- OTA round-trip on hardware with rollback test.
- `make flash` + curl smoke test documented in `docs/operations.md`.

## Files

`firmware/components/api/**`, `firmware/components/net/**`,
`docs/api/openapi.yaml` (only coordinated changes), `tools/hil.py`.

## Evidence

<!-- PR, curl transcript, OTA test log -->
