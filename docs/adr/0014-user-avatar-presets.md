# ADR 0014: Built-in user avatar presets

Status: Proposed

## Context

The booking UI identifies members using coloured initials. The administrator wants a lightweight, optional, more distinctive visual identifier without an image uploader, filesystem assets, runtime downloads, or substantial memory use. User details cross the frozen domain, service, and REST interfaces.

## Decision

- Add an integer `avatar_id` to the user record and admin REST API: `0` (initials-only; default), or `1..30` (preset).
- Persist avatar selection in user-created/updated/archived ledger events as `user_avatar_id`. Omit the field when zero to preserve legacy event hashes. Missing values default to zero when replaying.
- Preserve existing avatar selection on PATCH when `avatar_id` is absent, including archive/restore. Explicit zero resets to initials-only.
- Render the presets as five static colours × six decorative motifs in LVGL and the admin React app. Both render initials as the foreground. No image decoder, upload endpoint, asset bundle, or render-loop animation is required.
- Authorize avatar changes only through the existing authenticated administrator user-management API.
- Do not affect product selection, money, accounting, or backup schema versions.

## Consequences

The domain/service/OpenAPI contracts and all consumers are changed in one branch. User data and journal events remain self-contained and backup-compatible. The modest cost is extra fields in new user events and several LVGL widgets per avatar. The simulator and CI must verify compatibility, usability and build size.

## Alternatives considered

Uploading pictures would require image validation, file storage, image decoding and backup handling. Shipping bitmaps could be compact, but also entails binary assets and an image cache. Both add unnecessary complexity for 30 fixed designs.
