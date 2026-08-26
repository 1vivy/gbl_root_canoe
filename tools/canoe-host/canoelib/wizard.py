"""The single host entry point for interactive and scriptable Canoe work."""

from __future__ import annotations

import os
import time
from collections.abc import Sequence

from . import build, prep, prep_device, stage
from .errors import CanoeError
from .layout import Toolkit
from .oneshot import entry as oneshot_entry
from .ui import ask_choice, ask_yes_no, emit, note, run_entry, step
from .windows_ext4 import ensure as ensure_windows_ext4

_COMMANDS = ("build", "prep", "prep-device", "install", "oneshot")

_USAGE = """canoe - the Canoe host tool.

Run with no arguments for the interactive wizard, which is the intended path
for a person. The subcommands below are the same work without the questions,
for scripts and CI; each takes the flags its own --help lists.

  canoe                              interactive wizard
  canoe build                        patch the ABL and derive both sidecars
  canoe prep         [flags]         prepare alongside a firmware package
  canoe prep-device  [flags]         derive from the device's own abl/vbmeta
  canoe install      [flags]         install the boot root, then the BDS
  canoe oneshot --abl IMG --mode 0|1 temp-root a locked device; writes nothing

  canoe <command> --help             flags for one command
"""


def entry(argv: Sequence[str]) -> int:
    """Run the wizard, or dispatch one non-interactive subcommand."""
    return run_entry("canoe", _run, argv)


def _dispatch(command: str, argv: Sequence[str]) -> None:
    match command:
        case "build":
            result = build.entry(argv)
        case "prep":
            result = prep.entry(argv)
        case "prep-device":
            result = prep_device.entry(argv)
        case "install":
            result = stage.entry(argv)
        case "oneshot":
            result = oneshot_entry(argv)
        case _:
            raise CanoeError(f"unknown canoe command: {command}")
    if result != 0:
        raise CanoeError(f"canoe {command} failed")


def _ensure_windows_tools() -> None:
    """Fetch and verify the ext4 bridge before a Windows operation."""
    if os.name == "nt":
        step("Checking the Windows ext4 bridge")
        ensure_windows_ext4()


def _wait_for_images(toolkit: Toolkit) -> None:
    """Watch until both matching candidate images appear in the images folder."""
    if toolkit.abl_image.is_file() and toolkit.vbmeta_image.is_file():
        return
    step("Waiting for the stock firmware pair")
    emit(
        "Images folder is empty. Add images/abl.img and images/vbmeta.img. "
        "They MUST match the firmware version being booted and MUST be stock."
    )
    note(f"Watching {toolkit.images} until both files are populated...")
    while True:
        if toolkit.abl_image.is_file() and toolkit.vbmeta_image.is_file():
            return
        time.sleep(1)


def _interactive() -> None:
    toolkit = Toolkit.shipped()
    phase = ask_choice("Is this the first time or an update", ("first", "update"), "update")
    note("0 honest unlocked: hook-free passthrough")
    note("1 ABL fake locked: present a locked projection to Android")
    note("2 KM-SPSS profile: use the derived KeyMint and TrustZone profile")
    mode = int(ask_choice("Which mode", ("0", "1", "2"), "1"))
    if mode == 1:
        if not ask_yes_no(
            "A custom recovery must be grafted with the vbmeta tool, flashed, and returned here. Proceed?",
            True,
        ):
            note("No files were changed.")
            return
        if ask_yes_no("Patch vendor_boot to blacklist oplus_secure_guard_new?", False):
            note("vendor_boot patch selected; prepare it before installing the entry.")
    _wait_for_images(toolkit)
    if not ask_yes_no("Generate a boot entry from these matching stock files?", True):
        note("No files were changed.")
        return
    step(f"Deriving the {phase} boot entry")
    derived = build.derive(toolkit)
    note(f"Derived boot.efi and sidecars from {toolkit.abl_image} and {toolkit.vbmeta_image}")
    if stage.entry(("--mode", str(mode))) != 0:
        raise CanoeError("canoe install failed")
    emit(
        "Result:\n"
        f"  Installed boot.efi, sidecars, canoe.cfg, and tools/ under {toolkit.efisp}.\n"
        f"  BDS mode {mode} is selected for the {phase} entry; next boot launches the configured menu.\n"
        f"  GBL vulnerability patched: {'yes' if derived.gbl_patched else 'no'}"
    )


def _run(argv: Sequence[str]) -> None:
    """Choose interactive mode by default and dispatch explicit commands."""
    arguments = list(argv)
    # Answered before anything else runs: --help must not depend on a toolkit
    # being present, and on Windows it must not trigger the ext4 tool fetch.
    if arguments and arguments[0] in ("-h", "--help", "help"):
        emit(_USAGE)
        return
    _ensure_windows_tools()
    if not arguments:
        _interactive()
        return
    if arguments[0] == "--non-interactive":
        arguments.pop(0)
    if not arguments or arguments[0] not in _COMMANDS:
        raise CanoeError(
            f"unknown command {arguments[0]!r}\n\n{_USAGE}" if arguments
            else f"nothing to do\n\n{_USAGE}"
        )
    command = arguments.pop(0)
    _dispatch(command, arguments)
