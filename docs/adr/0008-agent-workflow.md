# ADR 0008 — Parallel agent workflow with worktrees and PRs

Status: accepted (2026-11-11)

## Context

Several AI agents will work on the repo concurrently. Uncoordinated edits to the
same files and giant PRs cause conflicts and regressions.

## Decision

- `main` is protected; no direct pushes.
- One task per worktree/branch: `feat/T-xxx-short`.
- Interfaces are frozen after M0: `domain.hpp`, `service.hpp`, `ui.hpp`,
  `net.hpp`, `openapi.yaml`, `settings.hpp`. Changes need an ADR and updates to
  all consumers.
- Task files in `docs/tasks/` are the board; each has owner, branch, status,
  acceptance criteria, test plan and evidence.
- One writer per module at a time; another agent reviews the PR.
- PRs are small, CI must be green, squash merge.
- Host tests, simulator and mock API let agents work without hardware.

## Consequences

- Slight upfront coordination cost; much lower merge/regression risk.
- Every change is reviewable and traceable to a task.
