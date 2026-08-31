"""The single host entry point for interactive and scriptable Canoe work."""

from __future__ import annotations

import time
from collections.abc import Sequence
from pathlib import Path

from . import bootmgr, build, sfb, stage
from .errors import CanoeError
from .layout import Toolkit
from .ui import ask_choice, ask_yes_no, emit, note, run_entry, step, warn

_COMMANDS = ("build", "install", "entry", "config", "default", "bls", "slot", "source")

_USAGE = """canoe - the Canoe host tool.

Run with no arguments for the interactive wizard, which is the intended path
for a person. The subcommands below are the same work without the questions,
for scripts and CI; each takes the flags its own --help lists.

  canoe                              interactive wizard
  canoe build [--abl IMG] [--vbmeta IMG]
                                     patch the ABL and derive both sidecars
  canoe install [flags]              install the boot root over USB Mass Storage
  canoe entry set|remove|mode ...    edit persisted canoe.cfg rows
  canoe config set-policy ...       set Silent/Menu boot policy
  canoe default get|set ...         inspect or change the default row
  canoe bls list|show|stage ...     inspect or stage BLS Type #1 entries
  canoe source detect ...           enumerate candidate boot-root sources
  canoe slot status ...             report active/inactive slot metadata
  canoe <command> --help             flags for one command
"""



def entry(argv: Sequence[str]) -> int:
    """Run the wizard, or dispatch one non-interactive subcommand."""
    return run_entry("canoe", _run, argv)


def _dispatch(command: str, argv: Sequence[str]) -> None:
    match command:
        case "build":
            result = build.entry(argv)
        case "install":
            result = stage.entry(argv)
        case "entry" | "config" | "default" | "bls" | "slot" | "source":
            bootmgr.route(Toolkit.shipped(), None, (command, *argv))
            result = 0
        case _:
            raise CanoeError(f"unknown canoe command: {command}")
    if result != 0:
        raise CanoeError(f"canoe {command} failed")



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

def _confirm_environment(toolkit: Toolkit) -> sfb.Identity | None:
    """Identify the device, warning when it is not Super Fastboot.

    Returns None when the operator declined to continue. A probe failure and an
    unrecognised device are separate warnings because they have separate
    remedies: the first means fastboot itself is missing, the second means the
    device is not in the BDS.
    """
    try:
        identity = sfb.identify(toolkit.root)
    except CanoeError as exc:
        # The interactive wizard always reaches massstorage.export(), so a
        # missing fastboot is certainly fatal later. Surfacing it before the
        # questionnaire beats failing after every question has been answered.
        warn(f"Could not identify the device with fastboot: {exc}")
        if not ask_yes_no("Continue despite the fastboot probe failure?", default=False):
            return None
        return sfb.Identity(None, None)
    if identity.bds_version is None:
        warn(
            "The device does not look like Super Fastboot; "
            "fastboot oem mass-storage:persist does not exist outside the BDS."
        )
        if not ask_yes_no("Continue without Super Fastboot detection?", default=False):
            return None
    return identity


def _install_with_signer_gate(arguments: list[str]) -> bool:
    """Install, offering the signer-change override once.

    False when the operator declined the override. A signer change is expected
    when moving to or from a custom ROM, so it is a question rather than a
    refusal; any other failure propagates untouched.
    """
    try:
        stage.install(arguments)
    except CanoeError as exc:
        if "vbmeta signer changed" not in str(exc):
            raise
        emit(str(exc))
        if not ask_yes_no(
            "The supplied vbmeta has a different signer than the installed generation. "
            "This is expected when moving to or from a custom ROM. Continue?",
            default=False,
        ):
            return False
        stage.install([*arguments, "--allow-new-signer"])
    return True


def _interactive() -> None:
    toolkit = Toolkit.shipped()
    _wait_for_images(toolkit)
    identity = _confirm_environment(toolkit)
    if identity is None:
        note("No files were changed.")
        return
    if identity.current_slot is None:
        slot = ask_choice("Which slot is currently active", ("a", "b"), "a")
        note("This labels the menu rows; if it is wrong, re-run the install with the correct slot.")
    else:
        slot = identity.current_slot
        note(f"Read active slot {slot} from the device rather than guessing.")
    mode = int(ask_choice("Which mode", ("0", "1", "2"), "1"))
    vendor_boot: Path | None = None
    if mode == 1:
        note("Graft with: vbmetaport <official recovery vbmeta> <custom recovery.img> <output.img>")
        note("The grafted output must not grow.")
        if not ask_yes_no(
            "Mode 1 requires grafting a custom recovery with the vbmeta tool, flashing it, "
            "and returning here. Declining cancels the installation. Proceed?",
            default=True,
        ):
            note("No files were changed.")
            return
        candidate = toolkit.images / "vendor_boot.img"
        if candidate.is_file() and ask_yes_no(
            "Patch vendor_boot to blacklist oplus_secure_guard_new?", default=False
        ):
            vendor_boot = candidate
    if not ask_yes_no("Generate a boot entry from these matching stock files?", default=True):
        note("No files were changed.")
        return
    step("Deriving the boot entry")
    build.derive(toolkit)
    note(f"Derived boot.efi and sidecars from {toolkit.abl_image} and {toolkit.vbmeta_image}")
    arguments = ["--slot", slot, "--mode", str(mode)]
    if vendor_boot is not None:
        arguments.extend(("--vendor-boot", str(vendor_boot)))
    if not _install_with_signer_gate(arguments):
        note("No files were changed.")
        return
    emit(
        "Data format is required. On a first-time installation it is not optional:\n"
        "Mode 1 projects a locked DeviceInfo to the OS, and the TEE will refuse the\n"
        "data key for userdata written under the previous state, so the old data is\n"
        "unreadable either way.\n"
        "\n"
        "On the device: main menu -> Reboot to Recovery -> FORMAT DATA.\n"
        "canoe.cfg carries devinfo-repair asneeded, so the lock-state repair happens\n"
        "on the next managed launch; formatting is what makes that state coherent."
    )


def _run(argv: Sequence[str]) -> None:
    """Choose interactive mode by default and dispatch explicit commands."""
    arguments = list(argv)
    if arguments and arguments[0] in ("-h", "--help", "help"):
        emit(_USAGE)
        return
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
