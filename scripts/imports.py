#!/usr/bin/env python3
"""Check and re-pin the repository's imported sources and artifacts."""
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
# ─── How to run ───
# python3 scripts/imports.py {list,check,mk,pin}

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import assert_never
from imports_check import check, list_rows, make_text
from imports_model import ManifestError, PinError
from imports_pin import pin


def main() -> int:  # noqa: BROAD_EXCEPT_OK
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    for name in ("list", "check", "mk"):
        subparsers.add_parser(name)
    pin_parser = subparsers.add_parser("pin")
    pin_parser.add_argument("id")
    pin_parser.add_argument("--version")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    try:
        match args.command:
            case "list":
                return list_rows(root)
            case "check":
                return check(root)
            case "mk":
                print(make_text(root), end="")
                return 0
            case "pin":
                return pin(root, args.id, args.version)
            case unreachable:
                assert_never(unreachable)
    except ManifestError as exc:
        print(f"manifest error: {exc}", file=sys.stderr)
        return 2
    except PinError as exc:
        print(f"pin error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
