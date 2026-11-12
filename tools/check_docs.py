#!/usr/bin/env python3
"""Check relative links in Markdown files.

External URLs are deliberately not fetched: the build must work offline and
must never depend on a network service. The checker validates only local file
and directory targets (including anchors after the file name).
"""

from __future__ import annotations

import argparse
import re
import sys
import urllib.parse
from pathlib import Path

LINK_RE = re.compile(r"(?<!!)\[[^\]]*\]\(([^)]+)\)")
IMAGE_RE = re.compile(r"!\[[^\]]*\]\(([^)]+)\)")
REFERENCE_RE = re.compile(r"^\s*\[[^\]]+\]:\s*(\S+)", re.MULTILINE)
FENCE_RE = re.compile(r"^(```|~~~).*?^\1", re.MULTILINE | re.DOTALL)
SKIP_DIRS = {
    ".git",
    "build",
    "node_modules",
    "dist",
    "managed_components",
    "generated",
    ".cache",
}


def strip_code(text: str) -> str:
    return FENCE_RE.sub("", text)


def local_target(raw: str) -> str | None:
    target = raw.strip()
    if target.startswith("<") and target.endswith(">"):
        target = target[1:-1]
    target = target.split()[0] if target else ""
    if not target:
        return None
    lowered = target.lower()
    if lowered.startswith(("http://", "https://", "mailto:", "tel:", "data:")):
        return None
    if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target):
        return None
    if target.startswith("#"):
        return None
    return urllib.parse.unquote(target.split("#", 1)[0])


def check_file(path: Path, root: Path) -> list[str]:
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        return [f"{path}: cannot read: {exc}"]
    text = strip_code(text)
    failures: list[str] = []
    for regex in (LINK_RE, IMAGE_RE, REFERENCE_RE):
        for match in regex.finditer(text):
            target = local_target(match.group(1))
            if target is None:
                continue
            relative = target.lstrip("/")
            candidate = (root / relative) if target.startswith("/") else (path.parent / relative)
            if not candidate.exists():
                failures.append(f"{path.relative_to(root)}: missing link target {target!r}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."), help="repository root")
    args = parser.parse_args()
    root = args.root.resolve()

    files = [
        path
        for path in root.rglob("*.md")
        if not any(part in SKIP_DIRS for part in path.relative_to(root).parts)
    ]
    failures: list[str] = []
    for path in sorted(files):
        failures.extend(check_file(path, root))

    if failures:
        print("check_docs: broken relative links:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print(f"check_docs: ok ({len(files)} Markdown files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
