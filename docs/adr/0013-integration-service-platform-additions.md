# ADR 0013 — Integration additions to the service/platform contracts

Status: accepted (2026-11-12)

## Context

T-009 wires the components from T-001 (service), T-004 (API/net), T-005
(reports) and the board/UI into one firmware image. Three integration gaps
required additive contract changes:

1. `KaffeeService` owns the `Ledger` but exposed only copies of individual
   vectors. Report generation (T-005) needs a `const Ledger&` to run its
   aggregations and CSV writer.
2. `POST /factory-reset` must erase the journal/snapshots, the settings
   namespace and the network credentials. The API can reach the service and
   `IPlatform`, but neither exposed those operations.
3. `Service::save_settings()` applied display settings itself but had no way to
   hand the merged (secret-bearing) network/time settings to the platform
   without exposing secrets through `Settings settings() const`.

All three are additive: no existing method changed a signature or return type,
and every new interface method has a default implementation except the two
`KaffeeService` methods, whose only implementor is the service itself.

## Decision

1. `KaffeeService::ledger_snapshot()` returns a
   `std::shared_ptr<const Ledger>` copy taken under the service mutex. The
   commit hook is cleared and the const view prevents reports from mutating the
   live ledger. This mirrors ADR 0012's snapshot model: never hand out a
   reference that can be mutated after the mutex is released.
2. `KaffeeService::factory_reset()` erases the event store and settings store,
   clears sessions and in-memory state, and deliberately does **not** generate
   replacement credentials. The subsequent reboot goes through the first-boot
   path and generates a fresh setup password/AP password.
3. `IPlatform` gains default no-op hooks
   `apply_network_settings(const NetworkSettings&)` and
   `apply_time_settings(const TimeSettings&)` plus
   `reset_network_credentials()`. The device adapter forwards them to
   `NetworkManager`; host fakes keep compiling unchanged. The service calls the
   two apply hooks from `save_settings()` and `import_backup()`.
4. `ISettingsStore` gains default methods for the initial admin password
   (`load_setup_password`, `save_setup_password`, `clear_setup_password`). The
   NVS store keeps it in a dedicated `setup_password` key, separate from the
   `Settings` JSON blob, so backups and the event journal never contain it. The
   service persists it before the generated credentials, loads it on boot while
   `must_change_password` is true, and clears it immediately after a successful
   `change_admin_password()`. A device upgraded from firmware that kept the
   password only in RAM has no key; the service rotates the admin credentials
   and generates a fresh setup password instead of locking the admin out.
5. `IUiBackend` gains additive `must_change_admin_password()` and
   `setup_password()`. The device UI uses them to show an “Erste Einrichtung”
   card on page 3 (password, AP SSID/URL, change instruction) and to keep
   `must_change_admin_password()` true until the web change succeeds.

The API's factory-reset route is authenticated, CSRF-checked and requires
`X-Request-Id`. It returns 204 and runs the erase + reboot from a helper task so
the response can flush before the AP/HTTP server goes away.

## Consequences

- One `Ledger` copy per report request; acceptable for kiosk-sized histories.
- Host simulators need no changes unless they want to model network apply.
- A factory reset is best-effort: if a store reset fails the helper task logs it
  and still reboots. The device may need a second reset via the serial/recovery
  path on flash faults.
- `NetworkSettings`/`TimeSettings` secrets never leave the service; the platform
  receives the merged values directly from inside the service mutex.
- The initial admin password is plaintext in NVS until the first successful
  change. Physical access to the device is the recovery boundary; the key is
  never exported in backups, the journal or API settings responses.
