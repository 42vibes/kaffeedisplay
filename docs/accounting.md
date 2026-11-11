# Accounting

The Kaffeekasse is a **post-pay tab** for members plus **cash sales for guests**.

## Semantics

- Members consume now and settle later. A member balance may be positive
  (credit) or negative (open amount).
- Admin records a payment (`topup`) when cash is handed over. That increases the
  member balance / reduces debt and increases the physical cash expectation.
- A member purchase debits the member's tab but does **not** change cash.
- A guest sale is paid cash immediately: it increases expected cash and guest
  revenue, and never touches a member balance.
- Expenses decrease expected cash. They never touch member balances.
- Corrections are `void` (reversal) events, never deletes or edits.
- An `adjustment` changes a member balance without changing cash and requires a
  note. Use it for write-offs or goodwill, not for cash payments.

## Formulas

```
balance(user)  = Σ events.amount_cents for user
cash_expected  = topups + guest_sales − expenses
                 (voids are included in each sum with the opposite sign)
prepaid        = Σ max(balance, 0)
debts          = Σ max(−balance, 0)
net_liabilities = prepaid − debts
equity         = cash_expected − net_liabilities
```

## Cash reconciliation (Kassensturz)

Admin enters the physically counted cash. The device stores a `CashCount` with
`counted_cents`, `expected_cents` and the difference. It does **not** alter
balances. If the difference should be booked, the admin records an expense
(category `Kassendifferenz`) or an adjustment with a note.

## Reports

- Consumption: member items/revenue and guest items/revenue, per user/product.
- Deposits: cash payments per member.
- Expenses: by category/vendor/date; voided rows visible.
- Cash flow: opening cash, deposits, guest sales, expenses, closing cash.
- Balances: current balance per member, prepaid/debt split.
- Transactions: line-level ledger with `payment` (`Guthaben` / `Bar (Gast)`) and
  void markers.
- Periods: ISO week (Mo–So), calendar month, calendar year, all time, custom.
  Boundaries use `Europe/Berlin`.
- Time quality is included (`Zeitqualität`); estimated timestamps are flagged.

## Excel-compatible CSV

Default `csv_excel`:

- UTF-8 BOM, `sep=;`, semicolon delimiter, CRLF.
- German dates `TT.MM.JJJJ`, decimal comma, no thousands separators.
- German headers; amounts in EUR and cents columns.
- Escape quotes; prefix formula-leading cells (`= + - @`) unless they are valid
  numbers.
- Stream rows; do not build the whole file in RAM.
- Filename: `kaffeekasse_YYYY-MM_<report>.csv`.

`csv_raw`: comma delimiter, dot decimals, ISO timestamps for scripts.

## Example

| Action | Member balance | Cash expected | Equity |
|---|---:|---:|---:|
| Start | 0 | 0 | 0 |
| Ada pays 10 € | +10.00 | +10.00 | 0 |
| Ada buys 0.50 € | +9.50 | +10.00 | +0.50 |
| Guest buys 0.80 € cash | +9.50 | +10.80 | +1.30 |
| Machine service 8 € | +9.50 | +2.80 | −6.70 |

Equity is the fund's net position, not a per-member number. A negative equity
means the fund has spent more than it has collected/consumed so far.
