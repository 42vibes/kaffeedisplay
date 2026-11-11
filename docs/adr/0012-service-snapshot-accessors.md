# ADR 0012 — Snapshot accessors, queued change notifications and AP lifecycle hook

Status: accepted (2026-11-11)

## Context

`KaffeeService` is shared by the LVGL task, the HTTP task and the simulator.
Its first implementation returned `const std::vector<...>&` from `users()`,
`products()`, `events()`, `expenses()` and `cash_counts()`. The service mutex
was released before the caller used the reference, so any concurrent mutation
could reallocate or destroy the underlying vector while the caller iterated it.

Two related lifecycle issues surfaced in review:

- Ledger change callbacks were invoked synchronously on whatever thread
  performed the mutation. The UI callback must never be called from the HTTP
  task; LVGL is only safe on its own task.
- Sessions were cleared on an explicit `stop_ap()`, but not when the network
  layer stopped the AP on its own timer.

## Decision

1. The service vector accessors return **copies**:
   `std::vector<User> users() const`, and likewise for products, events,
   expenses and cash counts. Callers get a stable snapshot; the cost is one
   allocation per refresh, which is acceptable for the expected data size.
2. Change notifications are **queued**. Each successful mutation sets a pending
   flag; a dedicated notifier thread coalesces pending changes and calls the
   registered `ChangeCallback` and `IPlatform::notify_ledger_changed()` outside
   the service mutex. The callback therefore never runs on the mutating thread.
3. `IPlatform` gains an additive, default no-op hook
   `set_ap_stopped_callback(std::function<void()>)`. The service registers it
   and drops all sessions and login backoff state when the platform reports an
   AP stop/auto-off. `stop_ap()` keeps its explicit clearing behaviour and calls
   the platform without holding the service mutex, because the platform may
   invoke the hook synchronously.

## Consequences

- `IUiBackend` and all consumers (UI, API, simulator) must hold the returned
  vectors or copy only the fields they need; they must not retain references
  across calls.
- Refreshes are coalesced, so a burst of bookings triggers one UI refresh
  instead of one per event. The UI still refreshes on its own LVGL task.
- Device platform adapters must implement the AP-stopped hook and a monotonic
  uptime in `SystemStats::uptime_s`; the service uses uptime (not wall time) for
  session and rate-limit deadlines.
- This is a frozen-interface change for `service.hpp`; it is limited to return
  types and the additive platform hook. No event or storage format changes.
