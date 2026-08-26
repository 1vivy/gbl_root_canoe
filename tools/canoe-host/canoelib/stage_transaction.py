"""Hand the install over to the device script and read back its receipt.

The transaction itself belongs to `canoe_device_install.sh`; this module owns
only the hand-off, the receipt and the recovery artifact it leaves behind.
"""

from __future__ import annotations

import shlex
import sys
from dataclasses import dataclass
from typing import Final

from .adb import Adb
from .errors import CanoeError
from .layout import Toolkit
from .ui import emit

# The device script's own marks. `done` is its last line and it prints only on
# success, so it is the receipt; `first-install` distinguishes a device that had
# no previous generation to demote.
DONE_MARK: Final = "CANOE-MARK: done"
FIRST_INSTALL_MARK: Final = "CANOE-MARK: first-install"

NO_SIGNOFF: Final = "the device-side install never signed off; adb's exit status is no proof"


@dataclass(frozen=True, slots=True)
class Context:
    """Everything the transaction needs to address one device."""

    adb: Adb
    toolkit: Toolkit
    stage: str
    boot_root: str

@dataclass(frozen=True, slots=True)
class Receipt:
    """What the device-side transaction reported about itself."""

    code: int
    completed: bool
    first_install: bool

def quote(path: str) -> str:
    """Quote a device-shell path, leaving ordinary paths readable in logs."""
    return shlex.quote(path)


def _output(out: str, err: str) -> None:
    """Forward the device script's own output; its marks are the record."""
    if out:
        emit(out.rstrip("\n"))
    if err:
        print(err, file=sys.stderr, end="", flush=True)


def run_transaction(context: Context, mode: int, boot_entry: str) -> Receipt:
    """Invoke the device-side install and report what it said about itself."""
    args = [quote(context.stage), quote(context.boot_root)]
    script = quote(f"{context.stage}/canoe_device_install.sh")
    command = (
        f"CANOE_MODE={quote(str(mode))} "
        f"CANOE_BOOT_ENTRY={quote(boot_entry)} "
        f"sh {script} {' '.join(args)}"
    )
    result = context.adb.shell(command)
    _output(result.out, result.err)
    return Receipt(
        code=result.code,
        completed=DONE_MARK in result.out,
        first_install=FIRST_INSTALL_MARK in result.out,
    )


def check(receipt: Receipt) -> None:
    """Fail unless the transaction both exited zero and signed off.

    Both halves are required. A zero exit status is not proof of anything here:
    an adbd without shell protocol v2 does not propagate the remote status at
    all, so `adb shell` returns 0 whatever happened on the device. Without the
    script's own final mark, a run that rolled back would be reported as a
    successful install - and the operator would reboot onto the old chain
    believing it was the new one.
    """
    if receipt.code != 0:
        raise CanoeError(f"the device-side install failed and rolled back (exit {receipt.code})")
    if not receipt.completed:
        raise CanoeError(NO_SIGNOFF)

