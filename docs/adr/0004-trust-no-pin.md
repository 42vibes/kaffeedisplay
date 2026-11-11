# ADR 0004 — Trust-based device, no user PINs

Status: accepted (2026-11-11)

## Context

The device sits in a shared kitchen. Frictionless booking matters more than
preventing a colleague from charging the wrong account.

## Decision

No PINs, passwords or user sessions on the device. A user selects their name and
taps a drink; the booking is immediate. Guests pay cash and are recorded as
`guest_sale`. Only the single admin account authenticates, on the web.

## Consequences

- Mis-bookings are corrected with an 8 s undo (members) or an admin void.
- Admin dashboard highlights debts and recent activity.
- Physical access is explicitly trusted; the device must not expose admin
  functions.
