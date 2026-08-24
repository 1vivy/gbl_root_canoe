"""Set the preferred BDS mode after the boot-chain transaction."""

from __future__ import annotations

import os
import shlex
import sys
from dataclasses import dataclass
from typing import Final

from .adb import Adb
from .errors import CanoeError
from .layout import MODE_RECORD_SLACK, Toolkit
from .ui import emit, note, step

REMOTE_MODE: Final = "/tmp/canoe-mode2_profile"


@dataclass(frozen=True, slots=True)
class ModeRequest:
    """The already-read efisp geometry and the requested preferred mode."""

    mode: int
    device: str
    partition_bytes: int


def _q(path: str) -> str:
    """Quote a device-shell path while preserving ordinary-path logs."""
    return shlex.quote(path)


def _output(out: str, err: str) -> None:
    """Forward mode-tool output without hiding stderr diagnostics."""
    if out:
        emit(out.rstrip("\n"))
    if err:
        print(err, file=sys.stderr, end="", flush=True)


def set_preferred_mode(adb: Adb, toolkit: Toolkit, request: ModeRequest) -> None:
    """Write and reread one preferred-mode record without re-probing geometry."""
    step(f"Setting the preferred boot mode to {request.mode}")
    binary = toolkit.tool("mode2_profile-arm64")
    if os.name != "nt" and not os.access(binary, os.X_OK):
        raise CanoeError(
            f"--mode needs {binary} in the toolkit (built by 'make target_toolkit_android')"
        )
    size = request.partition_bytes
    if size < MODE_RECORD_SLACK:
        raise CanoeError(f"efisp is under 1 MiB ({size}); no room for the mode record")
    block = 4096 if size % 4096 == 0 else 512
    if size % block:
        raise CanoeError(f"efisp size {size} is not a multiple of 4096 or 512")
    note(f"{request.device}: {size} bytes, block size {block}")
    try:
        adb.push(binary, REMOTE_MODE)
        if not adb.shell(f"chmod 755 {_q(REMOTE_MODE)}").ok:
            raise CanoeError("could not chmod the pushed mode2_profile")
        command = (
            _q(REMOTE_MODE)
            + f" mode-write --device {_q(request.device)} --partition-bytes {size}"
            + f" --block-size {block} --mode {request.mode}"
        )
        result = adb.shell(command)
        _output(result.out, result.err)
        if not result.ok:
            raise CanoeError("mode-write failed")
        read = adb.shell(
            _q(REMOTE_MODE)
            + f" mode-read --device {_q(request.device)} --partition-bytes {size}"
            + f" --block-size {block}"
        )
        _output(read.out, read.err)
        if not read.ok:
            raise CanoeError("mode-read failed after the write")
        record = read.out.strip()
        expected = f"MODE={request.mode}|MODE_DEFAULTED=0"
        if record != expected:
            raise CanoeError(
                f"mode record reread does not show mode {request.mode} non-defaulted: {record}"
            )
        note(f"record reread: {record}")
    finally:
        adb.shell(f"rm -f {_q(REMOTE_MODE)}")
