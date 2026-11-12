#!/usr/bin/env python3
"""Hardware-in-the-loop smoke test for a connected Kaffeekasse.

Default mode is read-only: it checks the serial console and, when the admin API
is reachable, logs in and reads users/products. Ledger writes require an explicit
opt-in because they create audit-trail events on the real device::

    python3 tools/hil.py --allow-writes --password '...'
    HIL_ALLOW_WRITES=1 HIL_ADMIN_PASSWORD='...' python3 -m pytest tools/hil.py -q -s

The board is not reset and the AP is not started unless requested with
``--reset`` / ``--start-ap`` (or ``HIL_RESET=1`` / ``HIL_START_AP=1``). The
script skips with a clear message when the serial port is absent. Requirements:
``pip install -r tools/requirements-tools.txt``.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import time
import uuid
from pathlib import Path

try:
    import pytest
    import requests
except ImportError as exc:  # pragma: no cover - depends on local environment
    raise SystemExit(
        "hil: missing Python dependencies; run: "
        "python3 -m pip install -r tools/requirements-tools.txt"
    ) from exc

sys.path.insert(0, str(Path(__file__).resolve().parent))

from serial_proto import DEFAULT_BAUD, DEFAULT_PORT, CliError, SerialCli  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PORT = Path(os.environ.get("HIL_PORT", DEFAULT_PORT))
BAUD = int(os.environ.get("HIL_BAUD", DEFAULT_BAUD))
BASE_URL = os.environ.get("HIL_BASE_URL", "http://192.168.4.1/api/v1").rstrip("/")
ADMIN_USER = os.environ.get("HIL_ADMIN_USER", "admin")
ADMIN_PASSWORD = os.environ.get("HIL_ADMIN_PASSWORD", "")
TIMEOUT = float(os.environ.get("HIL_TIMEOUT", "10"))

pytestmark = pytest.mark.skipif(
    not PORT.exists(),
    reason=f"HIL skipped: serial port {PORT} not present (board not connected)",
)

# IDs are interpolated into a serial command; never allow whitespace or control
# characters to inject a second command.
SAFE_ID = re.compile(r"^[A-Za-z0-9_.:-]{1,64}$")


def _flag(name: str) -> bool:
    return os.environ.get(name) == "1"


def _serial() -> SerialCli:
    return SerialCli(
        str(PORT),
        BAUD,
        timeout=float(os.environ.get("HIL_TIMEOUT", "5")),
        verbose=os.environ.get("HIL_VERBOSE") == "1",
    )


def _flash() -> None:
    if not _flag("HIL_FLASH"):
        return
    print("hil: flashing firmware (HIL_FLASH=1)")
    result = subprocess.run(
        ["make", "-C", str(ROOT), "flash", f"PORT={PORT}"],
        text=True,
        timeout=600,
    )
    if result.returncode != 0:
        pytest.fail("flashing failed; see output above")


def _health(base_url: str) -> requests.Response | None:
    try:
        response = requests.get(base_url + "/health", timeout=3)
    except requests.RequestException:
        return None
    return response if response.status_code == 200 else None


def _ensure_http() -> str:
    """Return a reachable API base URL or skip the HTTP test.

    The AP is off by default. It is started over the serial CLI only when
    ``HIL_START_AP=1`` / ``--start-ap`` is set.
    """
    if _health(BASE_URL) is not None:
        return BASE_URL
    if _flag("HIL_START_AP"):
        try:
            with _serial() as cli:
                reply = cli.command("ap start", timeout=5)
            print(f"hil: AP start reply: {reply}")
        except CliError as exc:
            print(f"hil: AP start over serial failed: {exc}")
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if _health(BASE_URL) is not None:
                return BASE_URL
            time.sleep(1)
    pytest.skip(
        f"HIL HTTP checks skipped: no API at {BASE_URL} (join the AP/LAN, set "
        "HIL_BASE_URL, or pass --start-ap)"
    )


class Api:
    def __init__(self, base_url: str) -> None:
        self.base_url = base_url
        self.session = requests.Session()
        self.csrf = ""

    def url(self, path: str) -> str:
        return self.base_url + path

    def read(self, method: str, path: str, **kwargs: object) -> requests.Response:
        response = self.session.request(method, self.url(path), timeout=TIMEOUT, **kwargs)
        if response.status_code >= 500:
            print(f"hil: {method} {path} -> {response.status_code}: {response.text[:500]}")
        return response

    def write(self, method: str, path: str, **kwargs: object) -> requests.Response:
        headers = dict(kwargs.pop("headers", {}))  # type: ignore[arg-type]
        headers.setdefault("X-CSRF-Token", self.csrf)
        headers.setdefault("X-Request-Id", str(uuid.uuid4()))
        return self.read(method, path, headers=headers, **kwargs)

    def login(self) -> None:
        if not ADMIN_PASSWORD:
            pytest.fail(
                "HIL_ADMIN_PASSWORD is not set; set it to the admin password "
                "(or the first-boot setup password)"
            )
        response = self.read(
            "POST",
            "/auth/login",
            json={"username": ADMIN_USER, "password": ADMIN_PASSWORD},
        )
        assert response.status_code == 200, f"login failed: {response.status_code} {response.text}"
        me = self.read("GET", "/auth/me")
        assert me.status_code == 200, f"/auth/me failed: {me.status_code} {me.text}"
        self.csrf = me.json().get("csrf_token", "")
        assert self.csrf, "login succeeded but no csrf_token in /auth/me"

    def logout(self) -> None:
        try:
            self.write("POST", "/auth/logout")
        except requests.RequestException:
            pass


def _safe_id(value: str, label: str) -> str:
    if not SAFE_ID.fullmatch(value):
        pytest.fail(f"refusing to send unsafe {label} over serial: {value!r}")
    return value


def _parse_event_id(reply: str) -> str:
    parts = reply.split()
    if len(parts) < 3 or parts[1] != "booking":
        raise AssertionError(f"unexpected serial booking reply: {reply!r}")
    return _safe_id(parts[2], "event id")


def _serial_command(command: str) -> str:
    try:
        with _serial() as cli:
            # `book`/`void` are locked on the default firmware; unlock them with
            # the admin password for this boot (10 minute window). Older
            # firmware without the command simply ignores the ERR reply.
            if command.split(maxsplit=1)[0] in ("book", "void") and ADMIN_PASSWORD:
                try:
                    unlock = cli.command(f"unlock-writes {ADMIN_PASSWORD}", timeout=TIMEOUT)
                    if not unlock.startswith("OK"):
                        print(f"hil: serial write unlock returned {unlock!r}")
                except CliError as exc:
                    print(f"hil: serial write unlock skipped: {exc}")
            return cli.command(command, timeout=TIMEOUT)
    except CliError as exc:
        raise AssertionError(f"serial command {command!r} failed: {exc}") from exc


def _purchase(user_id: str, product_id: str) -> str:
    user_id = _safe_id(user_id, "user id")
    product_id = _safe_id(product_id, "product id")
    reply = _serial_command(f"book {user_id} {product_id}")
    if not reply.startswith("OK"):
        pytest.fail(f"serial book returned {reply!r}")
    return _parse_event_id(reply)


def _void(event_id: str, reason: str, *, strict: bool = True) -> bool:
    event_id = _safe_id(event_id, "event id")
    reason = re.sub(r"[\r\n\x00-\x1f]", " ", reason)[:120]
    try:
        reply = _serial_command(f"void {event_id} {reason}")
    except AssertionError as exc:
        if strict:
            raise
        print(f"hil: void cleanup skipped: {exc}")
        return False
    if not reply.startswith("OK"):
        message = f"serial void returned {reply!r}"
        if strict:
            pytest.fail(message)
        print(f"hil: void cleanup skipped: {message}")
        return False
    return True


def test_serial_boot_log() -> None:
    _flash()
    reset = _flag("HIL_RESET")
    try:
        with _serial() as cli:
            banner, lines = cli.boot_log(
                timeout=float(os.environ.get("HIL_BOOT_TIMEOUT", "15")), reset=reset
            )
            status_reply = ""
            if banner is None and not reset:
                # Do not reset implicitly. If the device is already booted, a
                # read-only status command proves the application is alive.
                try:
                    status_reply = cli.command("status")
                except CliError:
                    status_reply = ""
    except CliError as exc:
        pytest.fail(str(exc))

    if lines:
        print("hil: last serial lines:")
        for line in lines[-20:]:
            print(f"  {line}")
    if banner:
        print(f"hil: boot banner: {banner}")
        return
    if status_reply.startswith("OK"):
        pytest.skip("no fresh boot banner without reset; device answered 'status' (use --reset)")
    pytest.fail(
        f"no Kaffeekasse boot banner within timeout (port {PORT}); "
        "pass --reset to reboot the board"
    )


def test_api_read_only() -> None:
    api = Api(_ensure_http())
    try:
        api.login()
        print(f"hil: logged in to {api.base_url}")
        for path in ("/health", "/users", "/products", "/stats/summary", "/time"):
            response = api.read("GET", path)
            assert response.status_code == 200, f"{path}: {response.status_code} {response.text}"
        print("hil: read-only API checks passed")
    finally:
        api.logout()


def test_ledger_smoke(tmp_path: Path) -> None:
    if not _flag("HIL_ALLOW_WRITES"):
        pytest.skip("ledger writes disabled (set HIL_ALLOW_WRITES=1 / --allow-writes)")
    api = Api(_ensure_http())
    stamp = time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:6]
    user_id = product_id = purchase_id = topup_id = ""
    purchase_voided = False
    keep = _flag("HIL_KEEP")
    try:
        api.login()
        print(f"hil: logged in to {api.base_url}")

        response = api.write(
            "POST",
            "/users",
            json={"name": f"HIL Test {stamp}", "color": "#3366CC"},
        )
        assert response.status_code == 201, f"create user: {response.status_code} {response.text}"
        user_id = response.json()["id"]

        response = api.write(
            "POST",
            "/products",
            json={
                "name": f"HIL Test Kaffee {stamp}",
                "category": "Kaffee",
                "icon": "coffee",
                "color": "#A0522D",
                "price_cents": 150,
                "sort": 999,
            },
        )
        assert response.status_code == 201, f"create product: {response.status_code} {response.text}"
        product_id = response.json()["id"]

        purchase_id = _purchase(user_id, product_id)
        print(f"hil: purchase event {purchase_id}")

        response = api.write(
            "POST",
            f"/users/{user_id}/topup",
            json={"amount_cents": 500, "note": "HIL smoke test"},
        )
        assert response.status_code == 200, f"topup: {response.status_code} {response.text}"
        topup_id = response.json()["id"]

        balance = next(
            (
                user.get("balance_cents")
                for user in api.read("GET", "/users").json()
                if user.get("id") == user_id
            ),
            None,
        )
        assert balance == 350, f"expected balance 350 after purchase+topup, got {balance}"

        _void(purchase_id, "HIL smoke test")
        purchase_voided = True
        response = api.read("GET", f"/users/{user_id}/transactions", params={"limit": 20})
        assert response.status_code == 200, f"history: {response.status_code} {response.text}"
        events = response.json()
        assert any(event.get("reverses") == purchase_id for event in events), (
            "void event missing from user history"
        )

        response = api.read(
            "GET",
            "/reports/transactions",
            params={"period": "all", "format": "csv_excel"},
        )
        assert response.status_code == 200, f"CSV: {response.status_code} {response.text[:300]}"
        assert "text/csv" in response.headers.get("Content-Type", ""), (
            f"CSV content type: {response.headers.get('Content-Type')}"
        )
        body = response.content
        assert body.startswith(b"\xef\xbb\xbf"), "CSV is missing the UTF-8 BOM"
        assert b"sep=;" in body[:200], "CSV is missing the Excel sep=; line"
        assert b"\r\n" in body, "CSV does not use CRLF line endings"
        csv_path = tmp_path / "hil-report.csv"
        csv_path.write_bytes(body)
        print(f"hil: CSV report saved to {csv_path} ({len(body)} bytes)")
        print("hil: purchase, topup, void and CSV checks passed")
    finally:
        if keep:
            print("hil: HIL_KEEP=1, leaving test data and events in place")
        else:
            # The ledger is append-only: cleanup voids the test events and
            # archives the user/product. The audit trail remains in history.
            if purchase_id and not purchase_voided:
                _void(purchase_id, "HIL cleanup", strict=False)
            if topup_id:
                _void(topup_id, "HIL cleanup", strict=False)
            if product_id:
                response = api.write("DELETE", f"/products/{product_id}")
                print(f"hil: archive product -> {response.status_code}")
            if user_id:
                response = api.write("POST", f"/users/{user_id}/archive")
                print(f"hil: archive user -> {response.status_code}")
        api.logout()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=str(PORT))
    parser.add_argument("--baud", type=int, default=BAUD)
    parser.add_argument("--base-url", default=BASE_URL)
    parser.add_argument("--username", default=ADMIN_USER)
    parser.add_argument("--password", default=ADMIN_PASSWORD)
    parser.add_argument("--timeout", type=float, default=TIMEOUT)
    parser.add_argument(
        "--allow-writes",
        action="store_true",
        default=_flag("HIL_ALLOW_WRITES"),
        help="create/void ledger test data (default: read-only)",
    )
    parser.add_argument(
        "--reset",
        action="store_true",
        default=_flag("HIL_RESET"),
        help="reset the board to capture the boot banner",
    )
    parser.add_argument(
        "--start-ap",
        action="store_true",
        default=_flag("HIL_START_AP"),
        help="ask the device to start its AP",
    )
    parser.add_argument(
        "--flash",
        action="store_true",
        default=_flag("HIL_FLASH"),
        help="run 'make flash' before testing",
    )
    parser.add_argument(
        "--keep",
        action="store_true",
        default=_flag("HIL_KEEP"),
        help="do not void/archive test data",
    )
    parser.add_argument("--verbose", action="store_true", help="print all serial lines")
    args = parser.parse_args()

    os.environ["HIL_PORT"] = args.port
    os.environ["HIL_BAUD"] = str(args.baud)
    os.environ["HIL_BASE_URL"] = args.base_url
    os.environ["HIL_ADMIN_USER"] = args.username
    if args.password:
        os.environ["HIL_ADMIN_PASSWORD"] = args.password
    os.environ["HIL_TIMEOUT"] = str(args.timeout)
    os.environ["HIL_ALLOW_WRITES"] = "1" if args.allow_writes else "0"
    os.environ["HIL_RESET"] = "1" if args.reset else "0"
    os.environ["HIL_START_AP"] = "1" if args.start_ap else "0"
    os.environ["HIL_FLASH"] = "1" if args.flash else "0"
    os.environ["HIL_KEEP"] = "1" if args.keep else "0"
    if args.verbose:
        os.environ["HIL_VERBOSE"] = "1"

    if not Path(args.port).exists():
        print(f"hil: skipped: serial port {args.port} not present (board not connected)")
        return 0

    return int(pytest.main([str(Path(__file__).resolve()), "-q", "-s"]))


if __name__ == "__main__":
    raise SystemExit(main())
