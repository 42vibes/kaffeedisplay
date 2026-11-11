#!/usr/bin/env bash
set -euo pipefail

need() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing: $1"
        return 1
    fi
    echo "ok: $1 ($(command -v "$1"))"
}

echo "== toolchain check =="
need git
need cmake
need g++
need node
need npm
need python3
need clang-format || true
if command -v idf.py >/dev/null 2>&1; then
    echo "ok: idf.py ($(idf.py --version 2>/dev/null || true))"
elif command -v docker >/dev/null 2>&1; then
    echo "ok: docker (firmware builds use ${IDF_IMAGE:-espressif/idf:release-v5.5})"
else
    echo "warn: neither idf.py nor docker found; firmware build will not work"
fi

if [ -e /dev/ttyUSB0 ]; then
    echo "ok: board at /dev/ttyUSB0"
else
    echo "info: /dev/ttyUSB0 not present (flashing unavailable)"
fi

echo "== web dependencies =="
if [ -f web/package-lock.json ]; then
    npm --prefix web ci
else
    npm --prefix web install
fi

echo
echo "bootstrap complete. Try: make test"
