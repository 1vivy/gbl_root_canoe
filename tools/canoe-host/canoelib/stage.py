"""Install the prepared canoe boot root over ADB or USB Mass Storage."""

from __future__ import annotations

import argparse
import shutil
import tempfile
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Final, Literal

from . import massstorage
from .adb import Adb
from .config import Config, ConfigError, verify_config
from .device import find_persist
from .errors import CanoeError
from .layout import GM2P_BYTES, TZMAP_BYTES, Toolkit, require_exact, require_nonempty
from .proc import Completed, run
from .stage_report import stage_report
from .stage_transaction import Context, check, quote, run_transaction
from .ui import emit, note, run_entry, step

PROG: Final = "canoe install"


@dataclass(frozen=True, slots=True)
class Options:
    """Parsed non-interactive install options."""

    serial: str | None
    persist: str | None
    via: Literal["adb", "mass-storage"]
    boot_root: Path | None
    slot: str | None
    mode: int


class _ParsedArgs(argparse.Namespace):
    """Typed namespace mutated only while argparse parses one invocation."""

    serial: str | None = None
    persist: str | None = None
    via: Literal["adb", "mass-storage"] = "adb"
    boot_root: str | None = None
    slot: str | None = None
    mode: int = 1


def entry(argv: Sequence[str]) -> int:
    """Run canoe install."""
    return run_entry(PROG, _run, argv)


def _parse_mode(raw: str) -> int:
    """Parse the three mode values accepted by canoe.cfg."""
    if raw not in ("0", "1", "2"):
        raise argparse.ArgumentTypeError("must be 0, 1 or 2")
    return int(raw)


def _parse_slot(raw: str) -> str:
    """Parse a slot letter into the suffix the device scripts use."""
    if raw not in ("a", "b"):
        raise argparse.ArgumentTypeError("must be a or b")
    return f"_{raw}"


def _options(argv: Sequence[str]) -> Options:
    """Parse install options without consulting the device."""
    parser = argparse.ArgumentParser(
        prog=PROG,
        description="Install the prepared canoe boot root over ADB or USB Mass Storage.",
        epilog="ADB needs recovery ADB; Mass Storage needs fastboot BDS and --boot-root or export.",
        exit_on_error=False,
    )
    parser.add_argument("-s", "--serial", help="adb device serial")
    parser.add_argument("--persist", metavar="PATH", help="persist mount point (ADB only)")
    parser.add_argument(
        "--via", choices=("adb", "mass-storage"), default="adb", help="install channel"
    )
    parser.add_argument("--boot-root", metavar="PATH", help="already-mounted persist path")
    parser.add_argument("--slot", type=_parse_slot, metavar="a|b", help="active host-run slot")
    parser.add_argument("--mode", type=_parse_mode, default=1, metavar="0|1|2", help="entry mode")
    parsed = _ParsedArgs()
    try:
        parser.parse_args(argv, namespace=parsed)
    except argparse.ArgumentError as exc:
        raise CanoeError(str(exc)) from exc
    via: Literal["adb", "mass-storage"] = parsed.via
    if parsed.via == "mass-storage" and parsed.persist is not None:
        raise CanoeError("--persist is only available through the adb channel")
    if parsed.boot_root is not None:
        if parsed.via == "adb":
            via = "mass-storage"
        if parsed.persist is not None:
            raise CanoeError("--persist cannot be combined with --boot-root")
    return Options(
        parsed.serial,
        parsed.persist,
        via,
        Path(parsed.boot_root) if parsed.boot_root is not None else None,
        parsed.slot,
        parsed.mode,
    )


def _stage_files(toolkit: Toolkit) -> list[tuple[Path, str]]:
    """Everything a transaction needs, as (local file, staged name) pairs.

    Both shared scripts travel with the payload: the transaction invokes
    canoe_boot_entry.sh, so staging one without the other installs a tree whose
    canoe.cfg can never be written.
    """
    files: list[tuple[Path, str]] = [
        (toolkit.boot_efi, "boot.efi"),
        (toolkit.gm2p, "boot.efi.gm2p"),
        (toolkit.tzmap, "boot.efi.tzmap"),
    ]
    if toolkit.efisp_tools.is_dir():
        files.extend(
            (item, f"tools/{item.name}")
            for item in sorted(toolkit.efisp_tools.iterdir(), key=lambda p: p.name)
            if item.is_file()
        )
    files.extend(
        (
            (toolkit.device_install, "canoe_device_install.sh"),
            (toolkit.boot_entry, "canoe_boot_entry.sh"),
        )
    )
    return files


def _stage_inputs(context: Context) -> None:
    """Create the remote stage and push all files before validating it."""
    if not context.adb.shell(
        f"rm -rf {quote(context.stage)} && mkdir -p {quote(context.stage + '/tools')}"
    ).ok:
        raise CanoeError(f"could not create {context.stage}")
    for local, name in _stage_files(context.toolkit):
        context.adb.push(local, f"{context.stage}/{name}")
        note(name)


def _validate_stage(context: Context) -> None:
    """Validate every staged size before invoking the transaction script."""
    checks = [
        ("boot.efi", context.toolkit.boot_efi.stat().st_size, "boot.efi"),
        ("boot.efi.gm2p", GM2P_BYTES, "gm2p"),
        ("boot.efi.tzmap", TZMAP_BYTES, "tzmap"),
        (
            "canoe_device_install.sh",
            context.toolkit.device_install.stat().st_size,
            "install script",
        ),
        ("canoe_boot_entry.sh", context.toolkit.boot_entry.stat().st_size, "entry script"),
    ]
    for remote, expected, label in checks:
        actual = context.adb.size(f"{context.stage}/{remote}")
        if actual != expected:
            got = "none" if actual is None else str(actual)
            raise CanoeError(f"{label} did not land as {expected} bytes (got {got})")
    note("staged set validated on device")


def _stage_local(toolkit: Toolkit, staging: Path) -> None:
    """Copy the same staged set into a host directory for a local transaction."""
    for source, name in _stage_files(toolkit):
        destination = staging / name
        try:
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
        except OSError as exc:
            raise CanoeError(f"could not stage {name}: {exc}") from exc
        note(name)


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


def _verify_installed_config(path: Path) -> Config:
    """Confirm the installed canoe.cfg is exactly what the shared writer emits."""
    try:
        config = verify_config(path)
    except ConfigError as exc:
        raise CanoeError(f"installed canoe.cfg verification failed: {exc}") from exc
    note(f"canoe.cfg verified (generation {config.generation})")
    return config


def _active_slot(options: Options, toolkit: Toolkit) -> str:
    """The slot a host-run transaction must declare, since getprop is unavailable.

    On the device the script reads ro.boot.slot_suffix itself. Here nothing can
    be asked - the BDS publishes no slot over fastboot - so it is either given
    or taken from what prep-device recorded, never guessed.
    """
    if options.slot is not None:
        return options.slot
    try:
        raw = toolkit.slot_receipt.read_text(encoding="ascii").strip()
    except FileNotFoundError as exc:
        raise CanoeError(
            "cannot determine the active slot for a host-run install; pass --slot a or b"
        ) from exc
    except OSError as exc:
        raise CanoeError(f"could not read {toolkit.slot_receipt}: {exc}") from exc
    if raw not in {"_a", "_b"}:
        raise CanoeError(
            f"invalid source slot receipt {toolkit.slot_receipt}; pass --slot a or b"
        )
    return raw


def _run_adb(toolkit: Toolkit, options: Options) -> tuple[str, bool]:
    """Install over adb, running the transaction on the device."""
    adb = Adb.connect(toolkit, options.serial)
    step("Locating the persist mount")
    persist = find_persist(adb, options.persist)
    rwtest = quote(persist + "/.canoe.rwtest")
    if not adb.shell(f"touch {rwtest} && rm -f {rwtest}").ok:
        raise CanoeError(f"{persist} is not writable")
    note(f"persist: {persist} (writable)")
    boot_root = f"{persist}/efisp"
    context = Context(adb, toolkit, f"{boot_root}/.canoe.stage", boot_root)
    step(f"Staging into {context.stage}")
    try:
        _stage_inputs(context)
        _validate_stage(context)
        receipt = run_transaction(
            context,
            options.mode,
            f"{context.stage}/canoe_boot_entry.sh",
        )
    finally:
        adb.shell(f"rm -rf {quote(context.stage)}")
    check(receipt)
    with tempfile.TemporaryDirectory(prefix="canoe-verify-") as directory:
        config_path = Path(directory) / "canoe.cfg"
        adb.pull(f"{boot_root}/canoe.cfg", config_path)
        _verify_installed_config(config_path)
    return boot_root, receipt.first_install


def _run_mass_storage(toolkit: Toolkit, options: Options) -> tuple[str, bool]:
    """Install through a mounted persist export, running the transaction here."""
    active_slot = _active_slot(options, toolkit)
    handle: massstorage.Export | None = None
    try:
        if options.boot_root is None:
            step("Exporting persist over USB Mass Storage")
            handle = massstorage.export(Path("fastboot"))
        else:
            handle = massstorage.local_boot_root(options.boot_root)
        boot_root = handle.boot_root
        first_install = not (boot_root / "boot.efi").is_file()
        with tempfile.TemporaryDirectory(prefix="canoe-stage-") as directory:
            staging = Path(directory)
            _stage_local(toolkit, staging)
            step("Running the shared host-side install")
            massstorage.transaction(
                handle,
                staging,
                staging / "canoe_device_install.sh",
                mode=options.mode,
                active_slot=active_slot,
                boot_entry=staging / "canoe_boot_entry.sh",
            )
        _verify_installed_config(boot_root / "canoe.cfg")
        return str(boot_root), first_install
    finally:
        if handle is not None:
            massstorage.release(handle)


def _run(argv: Sequence[str]) -> None:
    """Run the complete host-side install for whichever channel was chosen."""
    options = _options(argv)
    toolkit = Toolkit.shipped()
    for path in (toolkit.boot_efi, toolkit.gm2p, toolkit.tzmap):
        require_nonempty(path, f"missing or empty: {path.relative_to(toolkit.root)}")
    require_nonempty(toolkit.device_install, "missing canoe_device_install.sh")
    require_nonempty(toolkit.boot_entry, "missing canoe_boot_entry.sh")
    require_exact(toolkit.gm2p, GM2P_BYTES, "boot.efi.gm2p")
    require_exact(toolkit.tzmap, TZMAP_BYTES, "boot.efi.tzmap")
    _verify_tzmap(toolkit)
    if options.via == "adb":
        destination, first_install = _run_adb(toolkit, options)
    else:
        destination, first_install = _run_mass_storage(toolkit, options)
    emit(
        stage_report(
            destination=destination,
            via=options.via,
            mode=options.mode,
            first_install=first_install,
        )
    )


