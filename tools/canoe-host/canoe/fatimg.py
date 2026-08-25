"""Recompute and verify canoe trailers on device-created FAT images."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Final, Sequence

from .errors import CanoeError
from .fatimg_trailer import TRAILER, Verification, parse_runs, trailer_bytes, verify_bytes
from .ui import emit, run_entry


def verify_image(image: Path, runs: Sequence[tuple[int, int]] = ()) -> Verification:
    """Verify an image trailer against optional physical runs."""
    try:
        raw = image.read_bytes()
    except OSError as exc:
        raise CanoeError(f"could not read image {image}: {exc}") from exc
    return verify_bytes(raw, runs)


def rewrite_trailer(image: Path, runs: Sequence[tuple[int, int]]) -> None:
    """Replace an image's trailer with a stamp for physical extent runs."""
    try:
        raw = image.read_bytes()
        image.write_bytes(raw[:-TRAILER] + trailer_bytes(len(raw), runs))
    except OSError as exc:
        raise CanoeError(f"could not rewrite trailer {image}: {exc}") from exc


def _run(argv: Sequence[str]) -> None:
    """Run the canoe_fatimg command."""
    parser = argparse.ArgumentParser(prog="canoe_fatimg")
    commands = parser.add_subparsers(dest="command", required=True)
    stamp = commands.add_parser("trailer")
    stamp.add_argument("image", type=Path)
    stamp.add_argument("runs", nargs="*")
    verify = commands.add_parser("verify")
    verify.add_argument("image", type=Path)
    verify.add_argument("runs", nargs="*")
    args = parser.parse_args(argv)
    if args.command == "trailer":
        rewrite_trailer(args.image, parse_runs(args.runs))
        emit(f"stamped {args.image}")
    else:
        report = verify_image(args.image, parse_runs(args.runs))
        if not report.ok:
            raise CanoeError("trailer mismatch: " + ", ".join(report.mismatches))
        emit("trailer verified")


def entry(argv: Sequence[str]) -> int:
    """Run canoe_fatimg."""
    return run_entry("canoe_fatimg", _run, argv)


if __name__ == "__main__":
    sys.exit(entry(sys.argv[1:]))
