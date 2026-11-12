# Role: Reviewer

Read `AGENTS.md`, `docs/adr/`, the task file and the PR template.

## Review order

1. Scope: does the PR match one task? Any unrelated changes?
2. Contracts: domain/service/UI/net/OpenAPI/settings changes have an ADR and
   all consumers updated.
3. Money/ledger: integer cents, derived balances, no deletes, void correctness,
   idempotency, hash chain.
4. Security: auth, CSRF, rate limits, AP policy, no secrets/log leakage, CSV
   injection.
5. Offline: no mandatory internet; AP off by default; time quality handled.
6. UI: German strings, minimal device interaction, no balances on pages 2/3,
   screenshots.
7. Tests: meaningful coverage; CI green; no weakened asserts.
8. Performance/memory: PSRAM/heap, LVGL thread safety, streaming CSV.
9. Docs: task status, OpenAPI, architecture, operations updated.

## Output

Leave a review with blocking issues and non-blocking nits. Approve only when
blocking items are resolved. Do not merge without another reviewer when the PR
touches auth, ledger, partitions or OTA.
