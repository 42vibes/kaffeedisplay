# Security

## Threat model

Physical access to the device is trusted. Anyone at the machine can book on any
member account; that is an explicit product decision. The admin web is the only
authenticated surface.

The AP is a local, demand-activated network. It may be exposed to people nearby
for the duration of the admin window, but never to the internet.

## Admin authentication

- Single account `admin`; password stored as PBKDF2-HMAC-SHA256 with a random
  16-byte salt, 50 000 iterations, constant-time compare. Password hashing runs
  outside the global service mutex.
- Initial password is generated on first boot, stored in a dedicated NVS key
  (plaintext, never in backups/journal/API settings) and shown on device page 3
  until the admin changes it. It survives reboots, so a reboot before the first
  change does not lock the admin out. The web SPA forces the change on the first
  login; the key is erased immediately after a successful change.
- Login rate limit: fixed 60 s failure window per client IP. After 5 failures the
  client backs off exponentially (5 s doubling up to 300 s); a successful login
  resets the counter. The first threshold breach writes one `admin_login_locked`
  audit event; individual failed attempts are counted in RAM and not written to
  the ledger.
- Session: 128-bit random token in RAM, `HttpOnly; SameSite=Strict` cookie,
  30 min idle / 8 h absolute. Deadlines are measured on monotonic uptime, so a
  backwards or unset wall clock cannot extend a session. Sessions and login
  backoff state are cleared when the AP stops or its timer expires.
- CSRF: `X-CSRF-Token` must match the session's separate 128-bit CSRF token on
  all mutating requests. The CSRF token is not the session token and is never
  accepted in place of it.
- Password reset requires physical access (serial CLI) or factory reset.

## Network

- AP off by default. Start only via 3 s long-press on page 3 or an authenticated
  LAN request. Default window 15 min, extendable, auto-off.
- WPA2-PSK, random default password, stable in NVS, shown only while the AP is
  active.
- Captive DNS answers only while the AP is active and only for probe paths /
  unknown hosts; never redirect API or asset requests.
- Plain HTTP only. No self-signed HTTPS (breaks captive-portal flows and phones).
- Optional STA + NAPT is explicit and off by default.
- mDNS `kaffeekasse.local`; the IP is the reliable fallback.

## Device UI

- No PINs, no login. Booking is trust-based.
- Admin functions are not reachable from the booking UI.
- Undo is limited to the last member booking for 8 s. Guest cash sales have no
  undo.
- Display test/rotation/brightness changes are admin-web only. Page 3 shows
  them read-only; its only control is the AP start/stop long-press.

## Data

- No cloud, telemetry or CDN.
- Journal hash chain detects corruption/tampering; it is not a secret-key MAC.
- Backups exclude admin/AP/STA secrets.
- CSV formula-injection protection.
- OTA images are accepted only through the authenticated admin API or USB.
  Signed releases are a future enhancement.

## Recovery

- Serial CLI (`status`, `time set`, `backup`, `book`, `factory-reset`) requires
  physical access; destructive commands require the admin password. Protocol:
  `docs/serial-cli.md`.
- Boot loop safety: after 3 consecutive crashes, start AP-only diagnostics mode
  and expose logs over the local web.
