# ADR 0010 — German-only UI, single admin, MIT

Status: accepted (2026-11-11)

## Context

The users and admin are German-speaking. One person administers the fund.

## Decision

- All user-visible UI and report/CSV text is German. Code, docs and identifiers
  are English.
- Exactly one admin account (`admin`); no roles or multi-admin in v1.
- Project license: MIT.

## Consequences

- Strings live in `web/src/i18n/de.ts` and `ui/src/strings.hpp`; adding another
  language is possible later without touching business logic.
- Admin password reset is a physical/serial recovery operation.
