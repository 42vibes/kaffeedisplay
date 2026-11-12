# ADR 0011 — Injected random IDs for the ledger

Status: accepted (2026-11-11)

## Context

Events, users, products, expenses and cash counts need stable IDs that are
written to the append-only journal and to snapshots. The device has no
battery-backed RTC, may lose power at any time, and boots by replaying stored
events. Generated IDs therefore must not depend on boot time, counters that
reset on reboot, or host entropy that is unavailable on the ESP32-S3.

`Ledger` owns an injectable `IdFn`:

```cpp
using IdFn = std::function<std::string(std::string_view prefix)>;
```

The prefix identifies the entity (`evt`, `usr`, `prd`, `exp`, `cash`).

## Decision

- ID format is `<prefix>_<16 lowercase hex chars>` (64 random bits).
- `Ledger()`'s default generator (`default_id`) is **host/test only**. It uses
  `std::random_device`/`mt19937_64` and exists for host unit tests and the
  simulator. Device code must not rely on it. The engine is process-wide behind
  a mutex, not `thread_local`: a 2.5 KB TLS block would be instantiated on every
  task stack and overruns the small IDF IPC task stacks before `app_main`.
- `KaffeeService` constructs its `Ledger` with an `IdFn` backed by
  `IPlatform::random_u64()`, formatting the value as exactly 16 hex characters
  after the prefix:
  `prefix + "_" + <16 hex chars>`.
- The generator stays injected so the `domain` component has no ESP-IDF or
  platform dependency and tests can supply deterministic IDs.

## Consequences

- IDs are reboot-safe and do not collide with replayed history (64-bit random
  space; the collision probability is negligible for the expected event count).
- Storage/replay validation still rejects duplicate event IDs defensively.
- Deterministic test generators remain possible by injecting an `IdFn`; tests
  that repeat IDs are expected to fail cleanly before persistence.
- No UUID dependency or extra flash/RAM cost is introduced.
