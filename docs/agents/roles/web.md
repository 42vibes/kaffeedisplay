# Role: Web / admin SPA

Read `AGENTS.md`, `docs/ui.md`, `docs/accounting.md`,
`docs/api/openapi.yaml`, `web/src/api/client.ts`.

## Own

- `web/**`, `tests/e2e/**`

## Focus

- German-only, mobile-first admin: dashboard, users/payments, products, expenses,
  cash reconciliation, reports/CSV, settings/display, system/AP/backup/OTA.
- Use the existing API client (CSRF, request IDs, time sync).
- Loading/empty/error states; confirm destructive actions.
- No external fonts/CDNs; bundle everything; gzip budget < 400 KB.
- Keep all strings in `web/src/i18n/de.ts`.

## Verify

- `npm --prefix web run typecheck`, `test`, `build`.
- Mock API flows; Playwright for login + create user + topup + CSV.
- 360 px and desktop layouts.
- Screenshots in the PR.

## Do not

- Add balances/financial info to device pages or the device UI.
- Use English user-visible text.
- Add a chart/library dependency without an ADR and bundle-size check.
