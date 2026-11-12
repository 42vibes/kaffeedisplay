# Report payload shape (admin API)

`GET /reports/{kind}` is typed as `additionalProperties: true` in
`docs/api/openapi.yaml`. To integrate the SPA with the backend report task
(T-005), the admin UI expects the following JSON shape. Send `format=json`
(the default) for this shape; CSV variants return `text/csv` instead.

## Envelope

```json
{
  "kind": "consumption",
  "period": "month",
  "from": 1761951600,
  "to": 1764543600,
  "time_quality": {
    "trusted": true,
    "source": "synced",
    "last_sync": 1762860000
  },
  "columns": [{ "key": "product_name", "label": "Getränk", "type": "text" }],
  "rows": [{ "product_name": "Kaffee" }],
  "totals": { "revenue_cents": 1250 }
}
```

- `from` is inclusive, `to` is exclusive, both Unix seconds.
- Period boundaries are calculated by the device in `Europe/Berlin`
  (ISO week Mo–So). For `period=custom`, the SPA sends Berlin-midnight
  boundaries as `from`/`to`; `to` is the midnight after the selected end date.
- `time_quality.trusted` is `true`, `false`, or absent. Absent means the SPA
  shows “Zeitqualität unbekannt” (neutral), never a green state.
- Money values are integer cents. Column types: `money`, `number`, `date`
  (Unix seconds, Berlin date), `datetime` (Unix seconds, Berlin date/time),
  `boolean`, `text`.
- `columns` is optional; the SPA derives columns from the first rows if absent.
- `rows` renders as a table.

## Summary reports

A report may omit `columns`/`rows` and return flat scalar fields or a `totals`
object. The SPA renders both as a key/value summary grid:

```json
{
  "kind": "overview",
  "period": "month",
  "from": 1761951600,
  "to": 1764543600,
  "time_quality": { "trusted": true },
  "cash_expected_cents": 5200,
  "prepaid_cents": 1670,
  "debts_cents": 350,
  "equity_cents": 3880
}
```

## Multi-table reports

Use `sections` when a report needs more than one table:

```json
{
  "kind": "cashflow",
  "sections": [
    {
      "title": "Kassenfluss",
      "columns": [{ "key": "ts", "label": "Datum", "type": "datetime" }],
      "rows": [{ "ts": 1762860000 }],
      "totals": { "closing_cash_cents": 5200 }
    }
  ]
}
```

## CSV

`format=csv_excel` uses UTF-8 BOM, `sep=;`, semicolons, CRLF, German headers
and decimal commas. `format=csv_raw` uses commas, dot decimals and ISO
timestamps. The SPA requests CSV with `Accept: text/csv` and names the download
`kaffeekasse_YYYY-MM_<kind>.csv`, using the month of the selected period start
in `Europe/Berlin`.

## Mock

`web/mock/server.mjs` implements this shape for all seven report kinds so the
SPA and Playwright flows stay deterministic.

## Implementation

The device implements this shape in `kaffee::reports::to_json()`: every report
kind returns its `kaffee::reports::columns()` table (stable keys such as
`amount_cents`, `timestamp`, `balance_cents`) and one row per aggregate/event.
Both EUR and cent money columns carry integer cents; the SPA renders either as
EUR. `time_quality` is added by the API from the platform clock. Standard
periods return `409 time_unset` while the device clock is unset; `period=custom`
with `from`/`to` still works because the caller supplies both bounds.

