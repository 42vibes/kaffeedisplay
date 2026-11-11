#!/usr/bin/env python3
"""Small client for the device's line-based serial CLI.

Protocol (also documented in ``docs/serial-cli.md``):

* 115200 baud, 8N1, UTF-8, commands terminated by ``\\n``.
* A command reply is a single line starting with ``OK`` or ``ERR``.
* ESP-IDF log lines and other output are ignored by clients.
* Commands never echo secrets unless explicitly requested.

This module is imported by ``tools/settime.py`` and ``tools/hil.py``.
"""

from __future__ import annotations

import re
import time
from dataclasses import dataclass, field
from pathlib import Path

try:  # keep import errors readable in --help paths
    import serial
except ImportError:  # pragma: no cover - depends on local environment
    serial = None  # type: ignore[assignment]

DEFAULT_PORT = "/dev/ttyUSB0"
DEFAULT_BAUD = 115200


class CliError(RuntimeError):
    """Raised when the serial port cannot be opened or the CLI misses a reply."""


SECRET_COMMANDS = {"factory-reset", "password", "admin-password", "wifi-password"}
SECRET_FIELD = re.compile(r"(?i)\b(password|token|secret|psk)=\S+")


def redact_command(command: str) -> str:
    name = command.split(maxsplit=1)[0].lower()
    if name in SECRET_COMMANDS or "password" in command.lower():
        return f"{name} <redacted>"
    return command


def redact_reply(line: str) -> str:
    if re.match(r"(?i)^OK\s+setup-password\b", line):
        return "OK setup-password <redacted>"
    return SECRET_FIELD.sub(r"\1=<redacted>", line)


@dataclass
class SerialCli:
    port: str = DEFAULT_PORT
    baud: int = DEFAULT_BAUD
    timeout: float = 3.0
    verbose: bool = False
    _port: object | None = field(default=None, init=False, repr=False)
    log: list[str] = field(default_factory=list, init=False, repr=False)

    def __enter__(self) -> "SerialCli":
        self.open()
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    def open(self) -> None:
        if serial is None:
            raise CliError(
                "pyserial is not installed; run: python3 -m pip install -r tools/requirements-tools.txt"
            )
        path = Path(self.port)
        if not path.exists():
            raise CliError(f"serial port {self.port} not found (is the board connected?)")
        try:
            self._port = serial.Serial(self.port, self.baud, timeout=0.25)
        except Exception as exc:  # noqa: BLE001 - pyserial raises several types
            raise CliError(f"cannot open {self.port}: {exc}") from exc

    def close(self) -> None:
        if self._port is not None:
            try:
                self._port.close()  # type: ignore[attr-defined]
            finally:
                self._port = None

    @property
    def is_open(self) -> bool:
        return self._port is not None

    def _write(self, command: str) -> None:
        if self._port is None:
            raise CliError("serial port is not open")
        self._port.write((command + "\n").encode("utf-8"))  # type: ignore[attr-defined]
        self._port.flush()  # type: ignore[attr-defined]
        print(f">>> {redact_command(command)}")

    def reset(self, hold_ms: int = 120) -> None:
        """Best-effort ESP32 reset through the CH340 DTR/RTS lines."""
        if self._port is None:
            raise CliError("serial port is not open")
        port = self._port
        port.dtr = False
        port.rts = True
        time.sleep(hold_ms / 1000)
        port.rts = False
        time.sleep(0.05)

    def read_line(self, timeout: float) -> str | None:
        if self._port is None:
            raise CliError("serial port is not open")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            raw = self._port.readline()  # type: ignore[attr-defined]
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue
            self.log.append(line)
            if self.verbose:
                print(f"<<< {redact_reply(line)}")
            return line
        return None

    def wait_for(self, pattern: str, timeout: float | None = None) -> str | None:
        regex = re.compile(pattern)
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while time.monotonic() < deadline:
            remaining = max(0.05, min(0.5, deadline - time.monotonic()))
            line = self.read_line(remaining)
            if line is not None and regex.search(line):
                return line
        return None

    def command(self, command: str, timeout: float | None = None) -> str:
        self._write(command)
        line = self.wait_for(r"^(OK|ERR)\b", timeout)
        if line is None:
            raise CliError(f"no reply to {command!r} within {timeout or self.timeout:.1f}s")
        return line

    def boot_log(
        self, timeout: float = 15.0, reset: bool = False
    ) -> tuple[str | None, list[str]]:
        """Read boot output; reset only when explicitly requested."""
        before = len(self.log)
        if reset:
            self.reset()
        pattern = re.compile(
            r"Kaffeekasse booting|Kaffeekasse.*ready|kaffee:.*boot",
            re.IGNORECASE,
        )
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self.read_line(0.5)
            if line is not None and pattern.search(line):
                return line, self.log[before:]
        return None, self.log[before:]
