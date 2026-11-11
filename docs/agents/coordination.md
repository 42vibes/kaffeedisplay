# Agent coordination

## Roles

| Role | File | Focus |
|---|---|---|
| Embedded | `docs/agents/roles/embedded.md` | board, display, touch, UI |
| Backend | `docs/agents/roles/backend.md` | domain, storage, service, API, net |
| Web | `docs/agents/roles/web.md` | admin SPA, reports UI |
| QA | `docs/agents/roles/qa.md` | tests, HIL, release, docs |
| Reviewer | `docs/agents/roles/reviewer.md` | diff review, contracts, security |

## Rules to avoid collisions

1. One writer per module at a time. Claim the task and check the board.
2. Do not edit frozen interfaces (`domain.hpp`, `service.hpp`, `ui.hpp`,
   `net.hpp`, `openapi.yaml`, `settings.hpp`) without an ADR and a PR that
   updates all consumers.
3. Generated files are owned by their generator; do not hand-edit.
4. Use a fresh worktree per task. Rebase on `main` before pushing.
5. Keep PRs small and scoped. Do not reformat unrelated code.
6. Cross-cutting changes (boot sequence, partition table, auth) need a reviewer
   from another track.
7. If blocked, write the blocker in the task file and pick another task.

## Handoff template

Add to the task file before pausing:

```
Status:
Branch:
Done:
Next:
Blockers:
Test evidence:
```

## Review checklist

- Domain invariants and money arithmetic (cents, no floats).
- No ledger mutation/deletion; void semantics correct.
- No cloud/CDN/telemetry; offline still works.
- German user-visible strings; no English leaks.
- Auth/CSRF/rate limits; no secrets in logs/backups.
- API matches OpenAPI; migrations/backups considered.
- Tests cover the change; CI green; screenshots for UI.
- Performance/memory budgets on device.
