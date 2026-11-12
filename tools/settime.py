#!/usr/bin/env python3
"""Set or read the device clock over the serial CLI.

Examples::

    python3 tools/settime.py --port /dev/ttyUSB0            # set to host UTC now
    python3 tools/settime.py --epoch 1762819200
    python3 tools/settime.py --status

Protocol (see ``docs/serial-cli.md``): send ``time set <epoch_seconds> <source>``
and expect ``OK time set <epoch_seconds>``; ``time`` replies with
``OK time <epoch_seconds> source=<source>``. The tool exits with status 2 when
the serial port is absent, 1 on a CLI error and 0 on success.
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from serial_proto import DEFAULT_BAUD, DEFAULT_PORT, CliError, SerialCli  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port", default=os.environ.get("ESPPORT", DEFAULT_PORT), help="serial port"
    )
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--epoch", type=int, help="Unix seconds to set (default: now)")
    parser.add_argument(
        "--source",
        default="manual",
        choices=("manual", "browser", "sntp"),
        help="time source recorded by the device",
    )
    parser.add_argument("--status", action="store_true", help="only print the current device time")
    parser.add_argument("--timeout", type=float, default=5.0, help="reply timeout in seconds")
    parser.add_argument("--dry-run", action="store_true", help="print the command only")
    parser.add_argument("--verbose", action="store_true", help="print every serial line")
    args = parser.parse_args()

    epoch = args.epoch if args.epoch is not None else int(time.time())
    if epoch < 1_600_000_000:
        print(f"settime: refusing implausible epoch {epoch}", file=sys.stderr)
        return 1

    command = "time" if args.status else f"time set {epoch} {args.source}"
    if args.dry_run:
        print(command)
        return 0

    if not Path(args.port).exists():
        print(f"settime: {args.port} not found (board not connected?)", file=sys.stderr)
        return 2

    try:
        with SerialCli(args.port, args.baud, args.timeout, args.verbose) as cli:
            reply = cli.command(command)
    except CliError as exc:
        print(f"settime: {exc}", file=sys.stderr)
        return 1

    print(reply)
    return 0 if reply.startswith("OK") else 1


if __name__ == "__main__":
    raise SystemExit(main())
