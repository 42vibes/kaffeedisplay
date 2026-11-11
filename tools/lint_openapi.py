#!/usr/bin/env python3
"""Minimal OpenAPI sanity check that runs without network access."""
from __future__ import annotations

import sys
from pathlib import Path

import yaml


def fail(message: str) -> None:
    print(f"openapi: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    path = Path(sys.argv[1] if len(sys.argv) > 1 else "docs/api/openapi.yaml")
    spec = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(spec, dict):
        fail("document is not a mapping")
    if not str(spec.get("openapi", "")).startswith("3."):
        fail("expected OpenAPI 3.x")
    if not spec.get("info", {}).get("title"):
        fail("info.title is required")
    paths = spec.get("paths")
    if not isinstance(paths, dict) or not paths:
        fail("no paths defined")
    for route, item in paths.items():
        if not route.startswith("/"):
            fail(f"invalid path {route}")
        if not isinstance(item, dict):
            fail(f"path {route} is not an object")
        for method in item:
            if method.lower() in {
                "get",
                "put",
                "post",
                "delete",
                "patch",
                "options",
                "head",
                "trace",
            }:
                if "responses" not in item[method]:
                    fail(f"{method.upper()} {route} has no responses")
    schemas = spec.get("components", {}).get("schemas", {})
    for name, schema in schemas.items():
        if not isinstance(schema, dict):
            fail(f"schema {name} is not an object")
    print(f"openapi: ok ({len(paths)} paths, {len(schemas)} schemas)")


if __name__ == "__main__":
    main()
