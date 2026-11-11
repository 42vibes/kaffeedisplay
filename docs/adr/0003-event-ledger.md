# ADR 0003 — Event-sourced, append-only ledger

Status: accepted (2026-11-11)

## Context

The fund must be auditable, survive power loss, and never silently lose or
rewrite history. Balances must always be explainable.

## Decision

Store immutable events in an append-only JSONL journal with a SHA-256 hash chain
and periodic A/B snapshots. Balances, cash and reports are derived. Corrections
are reversal (`void`) events. Products/users are archived; products can be
deleted because events snapshot names/prices.

## Consequences

- Slightly more storage and boot replay cost; mitigated by snapshots.
- No in-place edits; every correction has an author, reason and timestamp.
- Recovery can truncate to the last valid event and quarantine corruption.
