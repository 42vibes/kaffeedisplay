#!/usr/bin/env python3
"""Regenerate the LVGL fonts used by the device UI.

Output goes to ``firmware/components/ui/generated/`` and is gitignored. Font
sources are downloaded from pinned upstream tags into
``firmware/components/ui/fonts/`` (also gitignored) and verified by SHA-256.
Point ``KAFFEE_FONT_DIR`` at a directory containing the TTFs to work offline.

The subset covers ASCII, Latin-1 (German umlauts and ß), the euro sign, common
German punctuation and arrows. ``lv_font_conv`` is pinned; npx downloads exactly
``lv_font_conv@1.5.3`` on first use.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path

LV_FONT_CONV_VERSION = "1.5.3"

# ASCII, Latin-1 (includes ÄÖÜäöüß), en/em dash, curly quotes, bullet,
# ellipsis, euro and right arrow.
RANGES = (
    "0x20-0x7E",
    "0xA0-0xFF",
    "0x2013-0x2014",
    "0x2018-0x201E",
    "0x2022",
    "0x2026",
    "0x20AC",
    "0x2192",
)
SYMBOLS = "€ÄÖÜäöüß"


@dataclass(frozen=True)
class FontSpec:
    family: str
    filename: str
    url: str | None
    sha256: str
    sizes: tuple[int, ...]
    archive_member: str | None = None
    optional: bool = False


FONTS = {
    "montserrat": FontSpec(
        family="montserrat",
        filename="Montserrat-Medium.ttf",
        url="https://raw.githubusercontent.com/JulietaUla/Montserrat/v7.222/fonts/ttf/Montserrat-Medium.ttf",
        sha256="7ce96811837174f00c087b73332aed3f04a19069248ab46213d9ea05ff879cbc",
        sizes=(16, 20, 28),
    ),
    "inter": FontSpec(
        family="inter",
        filename="Inter-Regular.ttf",
        url="https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip",
        sha256="40d692fce188e4471e2b3cba937be967878f631ad3ebbbdcd587687c7ebe0c82",
        sizes=(14, 16),
        archive_member="extras/ttf/Inter-Regular.ttf",
        optional=True,
    ),
}

DEFAULT_FONT_DIR = Path("firmware/components/ui/fonts")
DEFAULT_OUT = Path("firmware/components/ui/generated")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download(url: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as temp:
        temp_path = Path(temp.name)
    try:
        print(f"fonts: downloading {url}")
        with urllib.request.urlopen(url, timeout=120) as response, temp_path.open("wb") as out:
            shutil.copyfileobj(response, out)
        temp_path.replace(destination)
    finally:
        temp_path.unlink(missing_ok=True)


def ensure_font(spec: FontSpec, font_dir: Path, offline: bool, force: bool) -> Path | None:
    target = font_dir / spec.filename
    if target.exists() and not force:
        actual = sha256_file(target)
        if actual == spec.sha256:
            return target
        print(
            f"fonts: {target} has the wrong checksum ({actual[:12]}...), re-downloading",
            file=sys.stderr,
        )
    if offline:
        message = f"fonts: {target} is missing or invalid and --offline was set"
        if spec.optional:
            print(message + " (skipping optional font)", file=sys.stderr)
            return None
        raise SystemExit(message)
    if spec.url is None:
        raise SystemExit(f"fonts: no download URL for {spec.family}; set KAFFEE_FONT_DIR")

    if spec.archive_member is None:
        download(spec.url, target)
    else:
        cache = font_dir / ".cache" / (spec.family + "-" + Path(spec.url).name)
        if not cache.exists() or force:
            download(spec.url, cache)
        with zipfile.ZipFile(cache) as archive:
            member = spec.archive_member
            if member not in archive.namelist():
                raise SystemExit(f"fonts: {member} not found in {cache}")
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(member) as source, target.open("wb") as out:
                shutil.copyfileobj(source, out)

    actual = sha256_file(target)
    if actual != spec.sha256:
        target.unlink(missing_ok=True)
        raise SystemExit(
            f"fonts: checksum mismatch for {target}: expected {spec.sha256}, got {actual}"
        )
    return target


def lv_font_conv_command() -> list[str]:
    if shutil.which("lv_font_conv"):
        return ["lv_font_conv"]
    if shutil.which("npx"):
        return ["npx", "--yes", f"lv_font_conv@{LV_FONT_CONV_VERSION}"]
    raise SystemExit(
        "fonts: neither lv_font_conv nor npx found. Install Node.js/npm and run again, "
        f"or install lv_font_conv@{LV_FONT_CONV_VERSION} globally."
    )


def font_name(family: str, size: int) -> str:
    return f"lv_font_{family}_de_{size}"


def generate(spec: FontSpec, source: Path, out_dir: Path, dry_run: bool) -> list[Path]:
    created: list[Path] = []
    for size in spec.sizes:
        name = font_name(spec.family, size)
        output = out_dir / f"{name}.c"
        command = [
            *lv_font_conv_command(),
            "--font",
            source.name,
            "--size",
            str(size),
            "--bpp",
            "4",
            "--format",
            "lvgl",
            "--lv-font-name",
            name,
            "--no-compress",
            "--symbols",
            SYMBOLS,
        ]
        for value in RANGES:
            command += ["-r", value]
        command += ["-o", f"{name}.c"]
        if dry_run:
            print("fonts:", " ".join(command))
            continue

        # Run from the font directory so the generator's "Opts" comment contains
        # stable relative names instead of machine-specific absolute paths.
        result = subprocess.run(command, cwd=source.parent, text=True, capture_output=True)
        if result.returncode != 0:
            raise SystemExit(
                f"fonts: lv_font_conv failed for {name}\n{result.stdout}\n{result.stderr}"
            )
        temporary = source.parent / f"{name}.c"
        output.parent.mkdir(parents=True, exist_ok=True)
        temporary.replace(output)
        created.append(output)
        print(f"fonts: wrote {output} ({output.stat().st_size / 1024:.1f} KiB)")
    return created


def write_header(generated: list[Path], out_dir: Path) -> Path:
    names = [path.stem for path in sorted(generated)]
    lines = [
        "// Generated by tools/gen_fonts.py -- do not edit.",
        "#pragma once",
        "",
        '#include "lvgl.h"',
        "",
    ]
    for name in names:
        lines.append(f"LV_FONT_DECLARE({name});")
    lines.append("")
    header = out_dir / "lv_fonts.hpp"
    header.write_text("\n".join(lines), encoding="utf-8")
    return header


def write_manifest(generated: list[Path], out_dir: Path) -> Path:
    manifest = {
        "generator": f"lv_font_conv@{LV_FONT_CONV_VERSION}",
        "ranges": list(RANGES),
        "symbols": SYMBOLS,
        "fonts": [
            {
                "file": path.name,
                "sha256": sha256_file(path),
                "bytes": path.stat().st_size,
            }
            for path in sorted(generated)
        ],
    }
    target = out_dir / "manifest.json"
    target.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return target


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--font-dir",
        type=Path,
        default=Path(os.environ.get("KAFFEE_FONT_DIR", DEFAULT_FONT_DIR)),
        help="directory containing (or receiving) the source TTFs",
    )
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="generated output directory")
    parser.add_argument(
        "--only",
        choices=sorted(FONTS),
        action="append",
        help="generate only the given family (repeatable)",
    )
    parser.add_argument("--offline", action="store_true", help="never download fonts")
    parser.add_argument("--force", action="store_true", help="re-download sources")
    parser.add_argument("--dry-run", action="store_true", help="print commands only")
    args = parser.parse_args()

    selected = args.only or sorted(FONTS)
    generated: list[Path] = []
    for family in selected:
        spec = FONTS[family]
        source = ensure_font(spec, args.font_dir, args.offline, args.force)
        if source is None:
            continue
        generated.extend(generate(spec, source, args.out, args.dry_run))

    if args.dry_run:
        return 0
    if not generated:
        print("fonts: nothing generated", file=sys.stderr)
        return 1
    print(f"fonts: {len(generated)} fonts in {args.out}")
    print(f"fonts: wrote {write_header(generated, args.out)}")
    print(f"fonts: wrote {write_manifest(generated, args.out)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
