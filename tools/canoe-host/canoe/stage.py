"""Install a prepared canoe boot chain through one cross-platform ADB driver."""

from __future__ import annotations

import argparse
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from .adb import Adb
from .device import find_persist, resolve_part
from .errors import CanoeError
from .layout import (
    GM2P_BYTES,
    TZMAP_BYTES,
    Toolkit,
    require_exact,
    require_nonempty,
)
from .stage_mode import ModeRequest, set_preferred_mode
from .stage_report import stage_report
from .stage_transaction import Context, check, pull_backup, quote, run_transaction
from .ui import emit, note, run_entry, step

PROG: Final = "canoe_stage"


@dataclass(frozen=True, slots=True)
class Options:  # noqa: D101
    serial: str | None
    persist: str | None
    install_bds: bool
    mode: int | None
    work: Path | None


Geometry = tuple[str, int]


def entry(argv: Sequence[str]) -> int:
    """Run canoe_stage."""
    return run_entry(PROG, _run, argv)


def _parse_mode(raw: str) -> int:
    """Parse the three mode values accepted by the device store."""
    match raw:
        case "0":
            return 0
        case "1":
            return 1
        case "2":
            return 2
        case _:
            # This is untrusted command-line text, so the parse default raises.
            raise argparse.ArgumentTypeError("must be 0, 1 or 2")


class _ParsedArgs(argparse.Namespace):
    """Typed namespace mutated only while argparse parses one invocation."""

    serial: str | None = None
    persist: str | None = None
    skip_bds: bool = False
    mode: int | None = None
    work: str | None = None


def _options(argv: Sequence[str]) -> Options:
    """Parse stage options without consulting the device."""
    parser = argparse.ArgumentParser(
        prog=PROG,
        description="Install the prepared canoe boot chain over ADB.",
        epilog=(
            "Needs only a custom recovery with ADB enabled: persist is writable there and no "
            "root on the running system is required. Expects, in the toolkit directory: "
            "efisp/boot.efi, efisp/boot.efi.gm2p, efisp/boot.efi.tzmap, efisp/BOOTENTRIES, "
            "efisp/tools/, BDS.efi and canoe_device_install.sh. Never touches the abl "
            "partition."
        ),
        exit_on_error=False,
    )
    parser.add_argument("-s", "--serial", help="adb device serial")
    parser.add_argument(
        "--persist",
        metavar="PATH",
        help="persist mount point (default: autodetect /persist, then /mnt/vendor/persist)",
    )
    parser.add_argument(
        "--skip-bds",
        action="store_true",
        help="install the persist tree only; do not write efisp",
    )
    parser.add_argument(
        "--mode",
        type=_parse_mode,
        metavar="0|1|2",
        help=(
            "after a successful install, set the preferred boot mode on efisp "
            "(0=honest unlocked, 1=ABL fake locked, 2=KM/SPSS profile); "
            "needs bin/mode2_profile-arm64"
        ),
    )
    parser.add_argument("--work", metavar="DIR", help="local backup directory (default: ./work)")
    parsed = _ParsedArgs()
    try:
        parser.parse_args(argv, namespace=parsed)
    except argparse.ArgumentError as exc:
        raise CanoeError(str(exc)) from exc
    work = Path(parsed.work) if parsed.work else None
    return Options(parsed.serial, parsed.persist, not parsed.skip_bds, parsed.mode, work)


def _stage_inputs(context: Context) -> None:
    """Create the remote stage and push all files before validating it."""
    if not context.adb.shell(
        f"rm -rf {quote(context.stage)} && mkdir -p {quote(context.stage + '/tools')}"
    ).ok:
        raise CanoeError(f"could not create {context.stage}")
    files: list[tuple[Path, str]] = [
        (context.toolkit.boot_efi, "boot.efi"),
        (context.toolkit.gm2p, "boot.efi.gm2p"),
        (context.toolkit.tzmap, "boot.efi.tzmap"),
        (context.toolkit.bootentries, "BOOTENTRIES"),
    ]
    if context.toolkit.efisp_tools.is_dir():
        files.extend(
            (item, f"tools/{item.name}")
            for item in sorted(context.toolkit.efisp_tools.iterdir(), key=lambda p: p.name)
            if item.is_file()
        )
    files += [(context.toolkit.bds, "BDS.efi")] if context.install_bds else []
    files.append((context.toolkit.device_install, "canoe_device_install.sh"))
    for local, name in files:
        context.adb.push(local, f"{context.stage}/{name}")
        note(name)


def _validate_stage(context: Context) -> None:
    """Validate every staged size before invoking the transaction script."""
    checks: list[tuple[str, int, str]] = [
        ("boot.efi", context.toolkit.boot_efi.stat().st_size, "boot.efi"),
        ("boot.efi.gm2p", GM2P_BYTES, "gm2p"),
        ("boot.efi.tzmap", TZMAP_BYTES, "tzmap"),
    ]
    if context.install_bds:
        checks.append(("BDS.efi", context.toolkit.bds.stat().st_size, "BDS.efi"))
    for remote, expected, label in checks:
        actual = context.adb.size(f"{context.stage}/{remote}")
        if actual != expected:
            got = "none" if actual is None else str(actual)
            raise CanoeError(f"{label} did not land as {expected} bytes (got {got})")
    note("staged set validated on device")


def _geometry(context: Context, mode: int | None) -> Geometry | None:
    """Read the efisp geometry once, before the transaction, only if it is needed."""
    if not (context.install_bds or mode is not None):
        return None
    device = resolve_part(context.adb, "efisp", None)
    size = context.adb.partition_bytes(device)
    note(f"efisp device: {device} ({size} bytes)")
    return device, size


def _run(argv: Sequence[str]) -> None:
    """Run the complete host-side staging and install sequence."""
    options = _options(argv)
    toolkit = Toolkit.shipped()
    message = " (run canoe_prep or canoe_prep_device first)"
    for path in (toolkit.boot_efi, toolkit.gm2p, toolkit.tzmap, toolkit.bootentries):
        require_nonempty(path, f"missing or empty: {path.relative_to(toolkit.root)}{message}")
    if not toolkit.device_install.is_file():
        raise CanoeError("missing canoe_device_install.sh")
    require_exact(toolkit.gm2p, GM2P_BYTES, "boot.efi.gm2p")
    require_exact(toolkit.tzmap, TZMAP_BYTES, "boot.efi.tzmap")
    if options.install_bds:
        require_nonempty(
            toolkit.bds, "missing or empty: BDS.efi (use --skip-bds to install the tree only)"
        )
    work = options.work or toolkit.root / "work"
    work.mkdir(parents=True, exist_ok=True)
    adb = Adb.connect(toolkit, options.serial)
    step("Locating the persist mount")
    persist = find_persist(adb, options.persist)
    rwtest = quote(persist + "/.canoe.rwtest")
    if not adb.shell(f"touch {rwtest} && rm -f {rwtest}").ok:
        raise CanoeError(f"{persist} is not writable")
    note(f"persist: {persist} (writable)")
    boot_root = f"{persist}/efisp"
    context = Context(adb, toolkit, f"{boot_root}/.canoe.stage", boot_root, options.install_bds)
    step(f"Staging into {context.stage}")
    geometry: Geometry | None = None
    try:
        _stage_inputs(context)
        _validate_stage(context)
        geometry = _geometry(context, options.mode)
        install_step = "Running the device-side install"
        if not context.install_bds:
            install_step += " (tree only)"
        step(install_step)
        receipt = run_transaction(context, geometry[0] if geometry else None)
        if context.install_bds:
            pull_backup(context, work)
    finally:
        adb.shell(f"rm -rf {quote(context.stage)}")
    check(receipt)
    if options.mode is not None:
        if geometry is None:
            raise CanoeError("efisp geometry was not read")
        set_preferred_mode(
            adb,
            toolkit,
            ModeRequest(options.mode, geometry[0], geometry[1]),
        )
    emit(
        stage_report(
            destination=boot_root,
            install_bds=context.install_bds,
            mode=options.mode,
            first_install=receipt.first_install,
        )
    )
