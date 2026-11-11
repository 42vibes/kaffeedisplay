#!/usr/bin/env python3
"""Hardware-in-the-loop smoke test for a connected Kaffeekasse.

Run directly::

    python3 tools/hil.py --port /dev/ttyUSB0 --password '...'
    HIL_ADMIN_PASSWORD='...' python3 -m pytest tools/hil.py -q -s

The script skips with a clear message when the serial port is absent. HTTP
checks are skipped when neither the AP nor the LAN is reachable; use
``--base-url`` for a LAN address and ``--no-ap`` to avoid starting the AP.

Requirements: ``pip install -r tools/requirements-tools.txt``.
"""

from __future__ import annotations

import argparse
import os
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


def _serial() -> SerialCli:
    return SerialCli(
        str(PORT),
        BAUD,
        timeout=float(os.environ.get("HIL_TIMEOUT", "5")),
        verbose=os.environ.get("HIL_VERBOSE") == "1",
    )


def _flash() -> None:
    if os.environ.get("HIL_FLASH") != "1":
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

    The AP is off by default. Unless ``HIL_START_AP=0``, ask the serial CLI to
    start it and wait for the management address to come up.
    """
    if _health(BASE_URL) is not None:
        return BASE_URL
    if os.environ.get("HIL_START_AP", "1") == "1":
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
        f"HIL HTTP checks skipped: no API at {BASE_URL} (join the AP/LAN or set HIL_BASE_URL)"
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


def _parse_event_id(reply: str) -> str:
    parts = reply.split()
    if len(parts) < 3 or not parts[1] in {"booking", "ok"}:
        raise AssertionError(f"unexpected serial booking reply: {reply!r}")
    return parts[2]


def _purchase(api: "Api", user_id: str, product_id: str) -> str:
    """Book through the device UI-equivalent serial command.

    The REST API intentionally has no write endpoint for bookings (bookings are
    made on the device). HIL therefore uses the documented ``book`` command.
    """
    errors: list[str] = []
    try:
        with _serial() as cli:
            reply = cli.command(f"book {user_id} {product_id}", timeout=TIMEOUT)
        if reply.startswith("OK"):
            return _parse_event_id(reply)
        errors.append(f"serial book returned {reply}")
    except CliError as exc:
        errors.append(f"serial book failed: {exc}")

    # Forward-compatible fallback if an API-side booking endpoint is added.
    # The OpenAPI contract has no such route today, so this usually 404s.
    try:
        response = api.write(
            "POST",
            "/transactions",
            json={"user_id": user_id, "product_id": product_id},
        )
        if response.status_code in (200, 201):
            return response.json()["id"]
        errors.append(f"POST /transactions -> {response.status_code}")
    except requests.RequestException as exc:
        errors.append(f"POST /transactions failed: {exc}")

    pytest.fail(
        "purchase failed: the firmware must implement the serial 'book <user_id> "
        "<product_id>' command for HIL (" + "; ".join(errors) + ")"
    )


def test_serial_boot_log() -> None:
    _flash()
    try:
        with _serial() as cli:
            banner, lines = cli.boot_log(timeout=float(os.environ.get("HIL_BOOT_TIMEOUT", "15")))
    except CliError as exc:
        pytest.fail(str(exc))

    if lines:
        print("hil: last serial lines:")
        for line in lines[-20:]:
            print(f"  {line}")
    assert banner, f"no Kaffeekasse boot banner within timeout (port {PORT})"
    print(f"hil: boot banner: {banner}")


def test_api_smoke(tmp_path: Path) -> None:
    api = Api(_ensure_http())
    stamp = time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:6]
    user_id = product_id = purchase_id = topup_id = ""
    purchase_voided = False
    keep = os.environ.get("HIL_KEEP") == "1"
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

        purchase_id = _purchase(api, user_id, product_id)
        print(f"hil: purchase event {purchase_id}")

        response = api.write(
            "POST",
            f"/users/{user_id}/topup",
            json={"amount_cents": 500, "note": "HIL smoke test"},
        )
        assert response.status_code == 200, f"topup: {response.status_code} {response.text}"
        topup_id = response.json()["id"]

        response = api.read("GET", f"/users/{user_id}/transactions", params={"limit": 20})
        assert response.status_code == 200, f"history: {response.status_code} {response.text}"
        events = response.json()
        ids = {event.get("id") for event in events}
        assert purchase_id in ids, "purchase event missing from user history"
        assert topup_id in ids, "topup event missing from user history"

        balance = next(
            (user.get("balance_cents") for user in api.read("GET", "/users").json()
             if user.get("id") == user_id),
            None,
        )
        assert balance == 350, f"expected balance 350 after purchase+topup, got {balance}"

        response = api.write(
            "POST",
            f"/transactions/{purchase_id}/void",
            json={"reason": "HIL smoke test"},
        )
        assert response.status_code == 200, f"void: {response.status_code} {response.text}"
        reversal = response.json()
        assert reversal.get("reverses") == purchase_id, "void did not reference the purchase"
        purchase_voided = True

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

        assert purchase_id
        print("hil: purchase, topup, void and CSV checks passed")
    finally:
        if keep:
            print("hil: HIL_KEEP=1, leaving test data in place")
        else:
            if purchase_id and not purchase_voided:
                response = api.write(
                    "POST",
                    f"/transactions/{purchase_id}/void",
                    json={"reason": "HIL cleanup"},
                )
                print(f"hil: cleanup purchase -> {response.status_code}")
            if topup_id:
                response = api.write(
                    "POST",
                    f"/transactions/{topup_id}/void",
                    json={"reason": "HIL cleanup"},
                )
                print(f"hil: cleanup topup -> {response.status_code}")
            if product_id:
                response = api.write("DELETE", f"/products/{product_id}")
                print(f"hil: cleanup product -> {response.status_code}")
            if user_id:
                response = api.write("POST", f"/users/{user_id}/archive")
                print(f"hil: cleanup user -> {response.status_code}")
        api.logout()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=str(PORT))
    parser.add_argument("--baud", type=int, default=BAUD)
    parser.add_argument("--base-url", default=BASE_URL)
    parser.add_argument("--username", default=ADMIN_USER)
    parser.add_argument("--password", default=ADMIN_PASSWORD)
    parser.add_argument("--timeout", type=float, default=TIMEOUT)
    parser.add_argument("--flash", action="store_true", help="run 'make flash' before testing")
    parser.add_argument("--no-ap", action="store_true", help="never ask the device to start its AP")
    parser.add_argument("--keep", action="store_true", help="do not clean up test data")
    parser.add_argument("--verbose", action="store_true", help="print all serial lines")
    args = parser.parse_args()

    os.environ["HIL_PORT"] = args.port
    os.environ["HIL_BAUD"] = str(args.baud)
    os.environ["HIL_BASE_URL"] = args.base_url
    os.environ["HIL_ADMIN_USER"] = args.username
    if args.password:
        os.environ["HIL_ADMIN_PASSWORD"] = args.password
    os.environ["HIL_TIMEOUT"] = str(args.timeout)
    os.environ["HIL_START_AP"] = "0" if args.no_ap else "1"
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
