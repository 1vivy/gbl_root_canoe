"""Build, inspect and stamp the writable FAT16 efisp file window."""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Final, Sequence

from .errors import CanoeError
from .fat16 import build_fat16
from .fat16 import extract_files, list_files
from .fatimg_trailer import TRAILER, Verification, parse_runs, trailer_bytes, verify_bytes
from .ui import emit, run_entry

DEFAULT_SIZE: Final = 16 * 1024 * 1024


@dataclass(frozen=True, slots=True)
class BuildResult:
    """Summary of a completed image build."""

    files: int
    bytes_written: int
    filesystem_path: str


def build_image(staging: Path, output: Path, target_size: int = DEFAULT_SIZE) -> BuildResult:
    """Build a FAT16 image and append an empty-run canoe trailer."""
    result = build_fat16(staging, output, target_size, trailer_bytes)
    return BuildResult(result.files, result.bytes_written, result.filesystem_path)


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
    build = commands.add_parser("build")
    build.add_argument("staging", type=Path)
    build.add_argument("output", type=Path)
    build.add_argument("--size", type=int, default=DEFAULT_SIZE)
    stamp = commands.add_parser("trailer")
    stamp.add_argument("image", type=Path)
    stamp.add_argument("runs", nargs="*")
    verify = commands.add_parser("verify")
    verify.add_argument("image", type=Path)
    verify.add_argument("runs", nargs="*")
    listing = commands.add_parser("list")
    listing.add_argument("image", type=Path)
    extract = commands.add_parser("extract")
    extract.add_argument("image", type=Path)
    extract.add_argument("destination", type=Path)
    args = parser.parse_args(argv)
    if args.command == "build":
        result = build_image(args.staging, args.output, args.size)
        emit(f"built {args.output} ({result.bytes_written} bytes, {result.filesystem_path})")
    elif args.command == "trailer":
        rewrite_trailer(args.image, parse_runs(args.runs))
        emit(f"stamped {args.image}")
    elif args.command == "verify":
        report = verify_image(args.image, parse_runs(args.runs))
        if not report.ok:
            raise CanoeError("trailer mismatch: " + ", ".join(report.mismatches))
        emit("trailer verified")
    elif args.command == "list":
        for name, size in list_files(args.image):
            emit(f"{name}\t{size}")
    else:
        extract_files(args.image, args.destination)
        emit(f"extracted {args.image} to {args.destination}")


def entry(argv: Sequence[str]) -> int:
    """Run canoe_fatimg."""
    return run_entry("canoe_fatimg", _run, argv)


if __name__ == "__main__":
    sys.exit(entry(sys.argv[1:]))
