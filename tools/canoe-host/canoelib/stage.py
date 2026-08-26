"""Install a prepared canoe boot chain through one cross-platform ADB driver."""

from __future__ import annotations

import argparse
from collections.abc import Sequence
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Final

from .adb import Adb
from .config import Config, ConfigEntry, ConfigError, read_config, write_config
from .device import find_persist, resolve_part
from .errors import CanoeError
from .layout import GM2P_BYTES, TZMAP_BYTES, Toolkit, require_exact, require_nonempty
from .proc import Completed, run
from .stage_report import stage_report
from .stage_transaction import Context, check, pull_backup, quote, run_transaction
from .ui import emit, note, run_entry, step

PROG: Final = "canoe install"


@dataclass(frozen=True, slots=True)
class Options:
    """Parsed non-interactive staging options."""

    serial: str | None
    persist: str | None
    install_bds: bool
    mode: int
    work: Path | None


class _ParsedArgs(argparse.Namespace):
    """Typed namespace mutated only while argparse parses one invocation."""

    serial: str | None = None
    persist: str | None = None
    skip_bds: bool = False
    mode: int = 1
    work: str | None = None


def entry(argv: Sequence[str]) -> int:
    """Run canoe install."""
    return run_entry(PROG, _run, argv)


def _parse_mode(raw: str) -> int:
    """Parse the three mode values accepted by canoe.cfg."""
    if raw not in ("0", "1", "2"):
        raise argparse.ArgumentTypeError("must be 0, 1 or 2")
    return int(raw)


def _options(argv: Sequence[str]) -> Options:
    """Parse stage options without consulting the device."""
    parser = argparse.ArgumentParser(
        prog=PROG,
        description="Install the prepared canoe boot chain over ADB.",
        epilog=(
            "Needs a custom recovery with ADB enabled and expects efisp/boot.efi, its sidecars, "
            "BOOTENTRIES, canoe.cfg, BDS.efi and canoe_device_install.sh in the toolkit."
        ),
        exit_on_error=False,
    )
    parser.add_argument("-s", "--serial", help="adb device serial")
    parser.add_argument("--persist", metavar="PATH", help="persist mount point")
    parser.add_argument("--skip-bds", action="store_true", help="install the persist tree only")
    parser.add_argument(
        "--mode",
        type=_parse_mode,
        default=1,
        metavar="0|1|2",
        help="mode written into the installed canoe.cfg entry",
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
        (context.toolkit.canoe_cfg, "canoe.cfg"),
        (context.toolkit.bootentries, "BOOTENTRIES"),
    ]
    if context.toolkit.efisp_tools.is_dir():
        files.extend(
            (item, f"tools/{item.name}")
            for item in sorted(context.toolkit.efisp_tools.iterdir(), key=lambda p: p.name)
            if item.is_file()
        )
    if context.install_bds:
        files.append((context.toolkit.bds, "BDS.efi"))
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
        ("canoe.cfg", context.toolkit.canoe_cfg.stat().st_size, "canoe.cfg"),
    ]
    if context.install_bds:
        checks.append(("BDS.efi", context.toolkit.bds.stat().st_size, "BDS.efi"))
    for remote, expected, label in checks:
        actual = context.adb.size(f"{context.stage}/{remote}")
        if actual != expected:
            got = "none" if actual is None else str(actual)
            raise CanoeError(f"{label} did not land as {expected} bytes (got {got})")
    note("staged set validated on device")


def _check_verify(result: Completed, message: str) -> None:
    """Fail with `message`, quoting the verifier's diagnosis."""
    if result.ok:
        return
    detail = (result.err or result.out).strip()
    raise CanoeError(f"{message}: {detail}" if detail else message)


def _verify_tzmap(toolkit: Toolkit) -> None:
    """Verify the staged tzmap against its recorded unpatched ABL loader."""
    if not toolkit.abl_original.is_file():
        note("skipping ABL/tzmap consistency check: ABL_original.efi is unavailable")
        return
    _check_verify(
        run(
            [
                toolkit.tool("abl_tzmap"),
                "verify",
                "--sidecar",
                toolkit.tzmap,
                "--abl",
                toolkit.abl_original,
                "--allow-zero-digest",
            ]
        ),
        "abl_tzmap verify failed",
    )


def _config(toolkit: Toolkit, mode: int) -> None:
    """Generate the complete menu declaration that accompanies this install."""
    try:
        existing = read_config(toolkit.canoe_cfg) if toolkit.canoe_cfg.is_file() else None
    except ConfigError:
        existing = None
    if existing is None:
        config = Config((ConfigEntry("android-a", "Android (slot A)", "boot.efi", mode, "active"),), mode=mode, default="android-a")
    else:
        entries = tuple(
            replace(entry, mode=mode) if entry.image == "boot.efi" else entry for entry in existing.entries
        )
        if not any(entry.image == "boot.efi" for entry in entries):
            entries += (ConfigEntry("android-a", "Android (slot A)", "boot.efi", mode, "active"),)
        config = Config(entries, existing.generation, existing.timeout, existing.default, mode, existing.lockstate)
    try:
        generation = write_config(toolkit.canoe_cfg, config)
    except ConfigError as exc:
        raise CanoeError(str(exc)) from exc
    note(f"canoe.cfg generation: {generation}")


def _run(argv: Sequence[str]) -> None:
    """Run the complete host-side staging and install sequence."""
    options = _options(argv)
    toolkit = Toolkit.shipped()
    for path in (toolkit.boot_efi, toolkit.gm2p, toolkit.tzmap, toolkit.bootentries):
        require_nonempty(path, f"missing or empty: {path.relative_to(toolkit.root)}")
    require_nonempty(toolkit.device_install, "missing canoe_device_install.sh")
    require_exact(toolkit.gm2p, GM2P_BYTES, "boot.efi.gm2p")
    require_exact(toolkit.tzmap, TZMAP_BYTES, "boot.efi.tzmap")
    _config(toolkit, options.mode)
    require_nonempty(toolkit.canoe_cfg, "missing or empty: efisp/canoe.cfg")
    _verify_tzmap(toolkit)
    if options.install_bds:
        require_nonempty(toolkit.bds, "missing or empty: BDS.efi (use --skip-bds to install the tree only)")
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
    efisp_device = resolve_part(adb, "efisp", None) if options.install_bds else None
    context = Context(adb, toolkit, f"{boot_root}/.canoe.stage", boot_root, options.install_bds)
    step(f"Staging into {context.stage}")
    receipt = None
    try:
        _stage_inputs(context)
        _validate_stage(context)
        install_step = "Running the device-side install"
        if not context.install_bds:
            install_step += " (tree only)"
        step(install_step)
        receipt = run_transaction(context, efisp_device)
        if context.install_bds:
            pull_backup(context, work)
    finally:
        adb.shell(f"rm -rf {quote(context.stage)}")
    if receipt is None:
        raise CanoeError("device-side transaction did not run")
    check(receipt)
    emit(
        stage_report(
            destination=boot_root,
            install_bds=context.install_bds,
            mode=options.mode,
            first_install=receipt.first_install,
        )
    )
