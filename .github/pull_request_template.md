## Summary

<!-- What changed and why? Link the task file: docs/tasks/T-xxx... -->

## Task

- [ ] `docs/tasks/...` updated (owner/status/PR link)
- [ ] One module / coherent scope

## Testing

- [ ] `make test-host`
- [ ] `npm --prefix web test`
- [ ] `npm --prefix web run typecheck`
- [ ] `npm --prefix web run build`
- [ ] Firmware builds (`idf.py build` / CI)
- [ ] Hardware checked (if applicable, describe)

## UI

- [ ] German user-visible text
- [ ] Screenshot from simulator / browser attached (if UI changed)
- [ ] No connection/QR/help clutter on device booking page

## Contracts & security

- [ ] `docs/api/openapi.yaml` updated for API changes
- [ ] ADR added for architecture/contract changes
- [ ] No secrets, backups or generated assets committed
- [ ] No runtime cloud/CDN dependency
- [ ] Money is integer cents; balances derived; no ledger deletes

## Risks

<!-- Known limitations, follow-ups, migration notes. -->
