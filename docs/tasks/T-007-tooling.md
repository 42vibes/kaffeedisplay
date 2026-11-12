# T-007 — HIL tools, fonts/assets, release pipeline

Status: review
Track: qa
Owner: T-007 tooling agent
Branch: `feat/T-007-tooling`

## Context

We need reproducible builds, deterministic fonts/assets, hardware smoke tests and
release artifacts, without blocking feature work.

## Deliverables

1. `tools/gen_fonts.py` or shell script: pinned `lv_font_conv` version, Inter /
   Montserrat subset with German umlauts, `€`, digits, punctuation. Output to
   `firmware/components/ui/generated/` (gitignored) or committed with a
   generated header. Document the choice.

   > Implementation note: T-004 commits the Montserrat subsets in
   > `firmware/components/ui/src/fonts/`. That is the single source of truth; no
   > duplicate generator is kept in T-007.
2. Web asset embedding script: `web/dist` → compressed C array / `EMBED_FILES`
   manifest for `api`; verify gzip < 400 KB.
3. `tools/hil.py`: pytest/pyserial/requests smoke test:
   flash, boot log, AP or LAN, login, create temp user/product, purchase, topup,
   void, CSV download, cleanup.
4. `.github/workflows/release.yml`: tag → firmware build in Docker, web build,
   sha256, attach `firmware.bin`, `manifest.json`, changelog.
5. Pre-commit/`make fmt` for C++ + TS; docs link checker; OpenAPI lint in CI.
6. `tools/settime.py` serial clock set; serial CLI commands documented.

## Acceptance

- `make check` runs host tests, web typecheck/tests/build, OpenAPI lint, size
  budgets.
- Release workflow produces a flashable image and checksums from a tag.
- HIL script passes on the connected board (or is skipped clearly when absent).
- Fonts render umlauts/€ at 480×480.

## Files

`tools/**`, `.github/workflows/**`, `Makefile`, `docs/operations.md`,
`docs/testing.md`.

## Evidence

- `tools/embed_web.py` generates `firmware/components/webassets/generated/`
  (gitignored); the component exposes the `kaffee::api::Asset` /
  `kaffee_web_asset_lookup()` contract expected by the API. Verified
  deterministic with `--check`, links into `kaffeekasse.bin` (asset paths
  present), and firmware builds in `espressif/idf:release-v5.5`.
- Device fonts are the committed Montserrat subsets from the UI track
  (`firmware/components/ui/src/fonts/`, T-004); no duplicate generator is kept.
- `tools/hil.py` + `tools/requirements-tools.txt`; serial boot check, opt-in
  HTTP/ledger smoke test, clear skip without hardware. Hardware run on the
  connected board failed the boot-banner check because it runs non-Kaffeekasse
  firmware (see PR notes).
- `.github/workflows/release.yml` and CI size/docs checks; `make check` passes
  locally without IDF (firmware build skipped).
- `tools/settime.py` and `docs/serial-cli.md`.
- PR: https://github.com/Babo96/kaffeedisplay/pull/5
