# Task board

One task per worktree/branch. `main` is protected. Update the row and the task
file as you work; put the PR link under Evidence when done.

| ID | Title | Track | Depends | Owner | Status |
|---|---|---|---|---|---|
| T-001 | Domain, storage, service implementation | backend | foundation | backend-agent | done |
| T-002 | Admin SPA: all pages + German UI | web | foundation | web-agent | done |
| T-003 | Board bring-up + 3-page LVGL device UI | embedded | foundation | board-agent / ui-agent | review |
| T-004 | HTTP API, auth, AP/time, backup/OTA | backend | T-001 | net-agent / api-agent | in-progress |
| T-005 | Reports + Excel CSV backend | backend | T-001 | reports-agent | review |
| T-006 | LVGL simulator + screenshots + README | ui | T-003 | ui-agent | review |
| T-007 | HIL tools, fonts/assets, release pipeline | qa | foundation | tooling-agent | done |

## Workflow

1. Claim a task: set Owner, status `in-progress`, create the worktree.
2. Implement + test (`make check` or the relevant subset).
3. Update docs/OpenAPI/ADR in the same branch.
4. Push, open PR, request review. Do not merge your own PR without review.
5. After merge, set status `done` and add the PR link.

## Status values

`ready`, `in-progress`, `review`, `blocked`, `done`.
