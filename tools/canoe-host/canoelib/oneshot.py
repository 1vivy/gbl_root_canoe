"""Build a one-shot patched loader without touching a toolkit or device tree."""

from __future__ import annotations

import argparse
import shutil
import tempfile
from collections.abc import Sequence
from pathlib import Path
from typing import Final

from .errors import CanoeError
from .layout import Toolkit, require_nonempty
from .proc import Completed, run
from .ui import emit, note, run_entry, step

PROG: Final = "canoe oneshot"


class _ParsedArgs(argparse.Namespace):
    """Typed namespace mutated only while argparse parses one invocation."""

    abl: str | None = None
    mode: str | None = None
    out: str | None = None


def entry(argv: Sequence[str]) -> int:
    """Run the one-shot loader builder."""
    return run_entry(PROG, _run, argv)


def _parse(argv: Sequence[str]) -> tuple[Path, int, Path | None]:
    parser = argparse.ArgumentParser(
        prog=PROG,
        description="Build a temporary one-shot loader from a known-stock vulnerable ABL.",
        epilog="This command never writes efisp or the boot root; --out only selects a host file.",
        exit_on_error=False,
    )
    parser.add_argument("--abl", required=True, metavar="IMG", help="known-stock vulnerable ABL image")
    parser.add_argument("--mode", required=True, choices=("0", "1"), metavar="0|1")
    parser.add_argument("--out", metavar="IMG", help="host output path (default: system temporary file)")
    parsed = _ParsedArgs()
    try:
        parser.parse_args(argv, namespace=parsed)
    except argparse.ArgumentError as exc:
        raise CanoeError(str(exc)) from exc
    abl = Path(parsed.abl or "")
    if not abl.is_file():
        raise CanoeError(f"ABL image not found: {abl}")
    return abl, int(parsed.mode or "0"), Path(parsed.out) if parsed.out else None


def _check(result: Completed, message: str) -> None:
    if result.ok:
        return
    detail = (result.err or result.out).strip()
    raise CanoeError(f"{message}: {detail}" if detail else message)


def _run(argv: Sequence[str]) -> None:
    """Extract and patch in a temporary root, copying only the requested output."""
    abl, mode, configured_out = _parse(argv)
    toolkit = Toolkit.shipped()
    output = configured_out or Path(tempfile.gettempdir()) / "canoe-oneshot-boot.efi"
    with tempfile.TemporaryDirectory(prefix="canoe-oneshot-") as root_name:
        root = Path(root_name)
        step("Extracting the loader into a temporary root")
        _check(run([toolkit.tool("extractfv"), "-o", root, abl]), "extractfv failed")
        loader = root / "LinuxLoader.efi"
        require_nonempty(loader, "extractfv produced no LinuxLoader.efi")
        step("Patching the temporary loader")
        temporary_output = root / "boot.efi"
        _check(run([toolkit.tool("patch_abl"), loader, temporary_output]), "patch_abl failed")
        require_nonempty(temporary_output, "patch_abl produced no one-shot loader")
        try:
            output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(temporary_output, output)
        except OSError as exc:
            raise CanoeError(f"could not write one-shot output {output}: {exc}") from exc
    note(f"one-shot Mode {mode} loader: {output}")
    emit("No boot root or efisp bytes were written; flash this host file only for the next boot.")
