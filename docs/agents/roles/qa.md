# Role: QA / tooling

Read `AGENTS.md`, `docs/testing.md`, `docs/operations.md`.

## Own

- `tests/**`, `tools/**`, `.github/**`, release scripts, docs consistency.

## Focus

- Host/API/e2e coverage, fault injection, HIL smoke test, size budgets.
- Reproducible builds, pinned toolchains, release artifacts/checksums.
- Font/asset generation, screenshot pipeline, docs link check.
- Verify offline behavior, power-loss recovery, AP timeout, rotation/brightness.

## Verify

- `make check` from a clean clone.
- CI green on the PR; firmware artifact produced.
- HIL script passes with the board or skips clearly without it.
- No secrets/generated files committed.

## Do not

- Weaken tests to make a PR pass.
- Commit build output or `sdkconfig`.
- Change business semantics; file a backend task instead.
