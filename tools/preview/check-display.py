#!/usr/bin/env python3
"""Smoke test the visible X11 desktop in the actual noVNC preview container.

Check the simulator window exists and the real X framebuffer contains a
non-uniform rendered UI. This catches "black noVNC" regressions that the
headless SDL screenshot tests alone cannot detect.
"""
from __future__ import annotations

import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    env = {**os.environ, "DISPLAY": os.environ.get("DISPLAY", ":99")}
    window = subprocess.run(
        ["xdotool", "search", "--name", "Kaffeekasse Simulator"],
        env=env, text=True, capture_output=True, check=False,
    )
    if window.returncode != 0 or not window.stdout.strip():
        print("FAIL: simulator X11 window is not visible", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as temp:
        capture = Path(temp) / "desktop.xwd"
        xwd = subprocess.run(
            ["xwd", "-root", "-silent", "-out", str(capture)],
            env=env, text=True, capture_output=True, check=False,
        )
        if xwd.returncode != 0:
            print(f"FAIL: X11 framebuffer capture: {xwd.stderr}", file=sys.stderr)
            return 1
        data = capture.read_bytes()
    if len(data) < 100:
        print("FAIL: empty X11 framebuffer", file=sys.stderr)
        return 1

    header = struct.unpack(">25I", data[:100])
    offset, width, height, bits_per_pixel, stride = (
        header[0], header[4], header[5], header[11], header[12]
    )
    if width != 480 or height != 480 or bits_per_pixel not in (24, 32):
        print(
            f"FAIL: unexpected framebuffer geometry {width}x{height}, "
            f"{bits_per_pixel} bpp",
            file=sys.stderr,
        )
        return 1
    pixels = data[offset:offset + stride * height]
    step = bits_per_pixel // 8
    if len(pixels) < stride * height or step == 0:
        print("FAIL: truncated X11 framebuffer", file=sys.stderr)
        return 1

    # Sample ~10,000 actual pixels and verify the display is not an unpainted
    # all-black desktop or a single flat placeholder colour.
    samples = set()
    for y in range(0, height, 6):
        for x in range(0, width, 6):
            pos = y * stride + x * step
            samples.add(pixels[pos:pos + step])
    if len(samples) < 16:
        print(
            f"FAIL: apparent black/blank simulator: only {len(samples)} colours",
            file=sys.stderr,
        )
        return 1

    print(
        f"PASS: simulator window detected; {width}x{height} X11 image "
        f"contains {len(samples)} sampled colours"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
