# ADR 0014: Built-in user avatar presets

Status: Proposed

## Context

The booking UI identifies members using coloured initials. The administrator wants a lightweight, optional, more distinctive visual identifier without an image uploader, filesystem assets, runtime downloads, or substantial memory use. User details cross the frozen domain, service, and REST interfaces.

## Decision

- Add an integer `avatar_id` to the user record and admin REST API: `0` (initials-only or legacy user), or `1..30` (preset). For new users an omitted field requests a random preset; an explicit zero requests initials-only.
- Persist avatar selection in user-created/updated/archived ledger events as `user_avatar_id`. Omit the field when zero to preserve legacy event hashes. Missing values default to zero when replaying.
- Assign omitted avatar IDs on POST server-side using the platform random number generator; exclude the previously created user's preset to avoid immediate duplicates. Choose the value **after idempotency lookup** so retries do not reroll. The selected ID is persisted once in the user-created event. Explicit presets and initials-only remain admin-selectable.
- Preserve existing avatar selection on PATCH when `avatar_id` is absent, including archive/restore. Explicit zero resets to initials-only.
- Render the presets as five static colours × six decorative motifs in LVGL and the admin React app. Both render initials as the foreground. No image decoder, upload endpoint, asset bundle, or render-loop animation is required.
- Authorize avatar changes only through the existing authenticated administrator user-management API.
- Do not affect product selection, money, accounting, or backup schema versions.

## Consequences

The domain/service/OpenAPI contracts and all consumers are changed in one branch. User data and journal events remain self-contained and backup-compatible. The modest cost is extra fields in new user events and several LVGL widgets per avatar. The simulator and CI must verify compatibility, usability and build size.

## Alternatives considered

Uploading pictures would require image validation, file storage, image decoding and backup handling. Shipping bitmaps could be compact, but also entails binary assets and an image cache. Both add unnecessary complexity for 30 fixed designs.
