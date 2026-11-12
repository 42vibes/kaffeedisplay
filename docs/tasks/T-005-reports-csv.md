# T-005 — Reports + Excel-compatible CSV backend

Status: review
Track: backend
Owner: agent T-006
Branch: `feat/T-006-reports`

## Context

Reports are served by the device and rendered by the admin SPA. No report library
on the device; generate rows from ledger events and stream CSV.

## Deliverables

1. `components/reports` (or `service/reports`):
   - periods: ISO week (Mo–So), month, year, all, custom; Europe/Berlin/DST.
   - kinds: overview, consumption, deposits, expenses, cashflow, balances,
     transactions.
   - net of voids; guest vs member; time-quality column.
2. CSV writer:
   - `csv_excel`: UTF-8 BOM, `sep=;`, semicolon, CRLF, comma decimals,
     `TT.MM.JJJJ`, German headers, injection protection, cents + EUR columns.
   - `csv_raw`: comma, dot, ISO.
   - streaming API (no full-file buffering), correct content type/filename.
3. Wire `GET /reports/{kind}` JSON + CSV into API (T-004 can call this).
4. Golden-file host tests for each report/format and period boundaries.

## Acceptance

- CSV opens correctly in German Excel (BOM, delimiter, decimals, dates).
- Period boundary tests for week/month/year and DST.
- Voided transactions appear transparently; no silent deletion.
- Yearly export streams under a small fixed memory budget.

## Files

`firmware/components/reports/**` (new), `firmware/components/api/src/reports*`,
`tests/host/reports_test.cpp`, `docs/accounting.md`.

## Evidence

- PR: https://github.com/Babo96/kaffeedisplay/pull/2
- Host tests: 30 cases / 885 assertions (`make test-host`)
- Golden CSVs: `tests/host/golden/reports/`
