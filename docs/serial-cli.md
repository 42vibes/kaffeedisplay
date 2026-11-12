# Serial CLI

The USB-C port exposes the CH340 serial console at `/dev/ttyUSB0`. It is a
development, recovery and hardware-test interface; nothing at runtime depends on
it.

## Protocol

- 115200 baud, 8 data bits, no parity, 1 stop bit (8N1), UTF-8.
- Commands are single lines terminated by `\n` (CR is ignored).
- A command reply is one line starting with `OK` or `ERR`.
- Unsolicited ESP-IDF log lines (`I (1234) tag: ...`) may interleave; clients
  ignore everything that does not start with `OK` or `ERR`.
- Replies use `key=value` fields so they stay machine-readable.
- Destructive commands require the admin password (physical access is the
  recovery boundary). The CLI never echoes Wi-Fi or session secrets.

Interactive use:

```sh
python3 -m serial.tools.miniterm /dev/ttyUSB0 115200
```

Or set the clock with the helper:

```sh
python3 tools/settime.py                  # host UTC now
python3 tools/settime.py --status
python3 tools/settime.py --epoch 1762819200 --source manual
```

`settime.py` exits 2 when the port is absent, 1 on `ERR`/timeout and 0 on `OK`.

## Commands

| Command | Reply | Purpose |
|---|---|---|
| `help` | `OK help commands=...` | List commands (one line). |
| `status` | `OK status seq=<n> users=<n> products=<n> time=<epoch> time_source=<src> ap=<on\|off>` | Ledger and system summary. |
| `time` | `OK time <epoch> source=<src>` | Read the device clock. |
| `time set <epoch> [source]` | `OK time set <epoch>` | Set UTC seconds; source is `manual`, `browser` or `sntp` (default `manual`). |
| `ap start` | `OK ap start ttl=<seconds>` | Start the admin AP window (same policy as page 3). |
| `ap stop` | `OK ap stop` | Stop the AP and clear admin sessions. |
| `ap extend <seconds>` | `OK ap extend ttl=<seconds>` | Extend the AP window. |
| `setup-password` | `OK setup-password <password>` | Print the initial admin password until it has been changed. |
| `backup` | `OK backup <bytes>` | Begin a backup export; the JSON body follows until `END backup`. Routine backups should use the admin API. |
| `factory-reset <admin_password>` | `OK factory-reset` | Erase ledger, settings and secrets; reboots to first-boot state. |
| `book <user_id> <product_id> [request_id]` | `OK booking <event_id> <amount_cents>` | Test/HIL-only booking through the same service path as the touch UI. |
| `void <event_id> <reason...>` | `OK void <event_id>` | Test/HIL-only reversal through the service. |

`ERR <code> <message>` examples: `ERR not-found user`, `ERR auth required`,
`ERR unknown-command book`. A timeout is not an error reply; clients must fail
with a clear diagnostic instead of retrying destructive commands.

## Hardware tests

`tools/hil.py` uses this protocol for the boot banner and the `book` command.
See `docs/testing.md` for the HIL environment variables and skip conditions.
