# T-010 — Optional preset user avatars

Status: in-progress
Track: ui/web/domain
Owner: 42vibes
Branch: `feature/user-avatars`

## Scope

- 30 static firmware presets, five palettes × six simple motifs, and visible initials.
- Admin-only selection from the user editor, with initials-only fallback.
- Persist avatar IDs (0..30) using compatible user events.
- Update REST/OpenAPI, tests, ADR and documentation.
- Leave booking, money and balances unchanged.

## Acceptance

- Current users and old journal hashes work with avatar 0.
- Admin can select, save, clear and restore the avatar.
- A stale user PATCH that does not mention avatar does not clear it.
- IDs outside 0..30 are rejected and never committed.
- Admin web, simulator and ESP-IDF firmware compile; host/API tests pass.
- UI screenshot evidence accompanies the upstream pull request.

## Evidence

- Simulator seed now displays four preset avatars and the initials-only fallback.
- README and UI specification updated with instructions and preview references.
- Screenshot artifacts generated from the changed simulator and mock UI, rather than screenshots copied from `main`.
- Screenshot verification and CI results must pass before marking this task done.
