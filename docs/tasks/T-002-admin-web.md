# T-002 — Admin SPA: all pages + German UI

Status: ready
Track: web
Owner: –
Branch: `feat/T-002-admin-web`

## Context

`web/` has a working Vite/React/TS/Tailwind shell, API client, mock server,
German strings and a dashboard skeleton. This task builds the complete admin UI
against `docs/api/openapi.yaml` and the mock API.

## Pages

- Login (single `admin`), session handling, time sync on login.
- Übersicht: summary cards, alerts (time, debts, backup, storage), recent events,
  period chart, quick actions.
- Benutzer: list/search, create/edit/archive, balance, topup, adjustment,
  history, debt highlighting.
- Getränke: CRUD, category, price, icon/color picker, sort, archive/delete.
- Ausgaben: CRUD, categories, vendor, note, void.
- Kassensturz: expected vs counted, difference, history.
- Berichte: period selector (Woche/Monat/Jahr/Gesamt/Custom), tabs (Übersicht,
  Verbrauch, Einzahlungen, Ausgaben, Kassenfluss, Salden, Transaktionen), JSON
  tables and CSV Excel/raw downloads.
- Einstellungen: display rotation/brightness/timeouts/test pattern, AP/STA,
  time/timezone, guest toggle, debt limit, password.
- System: device stats, AP status/QR, logs, backup/restore, OTA, factory reset
  (with confirmations).
- Mobile-first, dark espresso theme, no CDN, German only.

## Requirements

- Use `request()` from `src/api/client.ts`; CSRF and request IDs are automatic.
- Send browser time on login (`syncTimeFromBrowser` already wired).
- Loading/empty/error states, optimistic disabled buttons while saving.
- No balances on device-related admin? Admin may see all; device pages don't.
- Bundle budget: total gzip < 400 KB.
- Unit tests for money formatting and critical components; Playwright e2e for
  login + create user + topup + report CSV.

## Files

`web/**`, `tests/e2e/**`, `docs/api/openapi.yaml` (only if a contract gap is
found — coordinate first).

## Acceptance

- `npm --prefix web run typecheck`, `test`, `build` green.
- `make e2e` green against the mock (if Playwright is set up).
- All strings German; no hardcoded English.
- Works at 360 px and desktop.
- Screenshots attached to PR.

## Evidence

<!-- PR link, screenshots -->
