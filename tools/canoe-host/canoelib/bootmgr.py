"""Thin process adapter for the canonical ``canoe-bootmgr`` binary."""

from __future__ import annotations

import json
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Final, TypeAlias

from .errors import CanoeError
from .layout import Toolkit
from .massstorage import Export
from .proc import Completed, run
from .ui import emit

JsonValue: TypeAlias = (
    str | int | float | bool | list["JsonValue"] | dict[str, "JsonValue"] | None
)

__all__: Final = ("InstallOptions", "InstallReceipt", "install", "route")


@dataclass(frozen=True, slots=True)
class InstallOptions:
    """Boot-manager install values selected by the host command line."""

    slot: str
    mode: int
    allow_new_signer: bool


@dataclass(frozen=True, slots=True)
class InstallReceipt:
    """The install fields needed by the host completion report."""

    first_install: bool
    generation: int
    signer_changed: bool


def _location(handle: Export) -> tuple[str, str]:
    """Select the canonical backend flag without implementing boot logic."""
    if handle.backend == "local":
        if handle.boot_root is None:
            raise CanoeError("local export has no boot-root directory")
        return "--boot-root", str(handle.boot_root)
    if handle.source is None:
        raise CanoeError("ext4 export has no source block device")
    return "--source", str(handle.source)


def _binary(toolkit: Toolkit) -> Path:
    """Resolve the packaged boot manager executable."""
    return toolkit.tool("canoe-bootmgr")


def _failure(result: Completed) -> CanoeError:
    detail = (result.err or result.out).strip()
    return CanoeError(detail or "canoe-bootmgr failed")


def _integer(value: JsonValue, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise CanoeError(f"canoe-bootmgr response has invalid {field}")
    return value


def _boolean(value: JsonValue, field: str) -> bool:
    if not isinstance(value, bool):
        raise CanoeError(f"canoe-bootmgr response has invalid {field}")
    return value


def _parse_install(result: Completed) -> InstallReceipt:
    """Parse only machine-consumed receipt fields from a successful response."""
    try:
        payload = json.loads(result.out)
    except json.JSONDecodeError as exc:
        raise CanoeError(f"canoe-bootmgr returned invalid JSON: {exc}") from exc
    if not isinstance(payload, dict) or payload.get("operation") != "install":
        raise CanoeError("canoe-bootmgr returned an unexpected install response")
    receipt = payload.get("receipt")
    if not isinstance(receipt, dict):
        raise CanoeError("canoe-bootmgr install response has no receipt")
    backup_present = _boolean(receipt.get("backup_present"), "backup_present")
    return InstallReceipt(
        first_install=not backup_present,
        generation=_integer(receipt.get("generation"), "generation"),
        signer_changed=_boolean(receipt.get("signer_changed"), "signer_changed"),
    )


def install(
    toolkit: Toolkit,
    handle: Export,
    staged: Path,
    options: InstallOptions,
) -> InstallReceipt:
    """Run the canonical install operation against either backend."""
    location_flag, location = _location(handle)
    command: list[str | Path] = [
        _binary(toolkit),
        "--json",
        location_flag,
        location,
        "install",
        "--staged",
        staged,
        "--slot",
        options.slot,
        "--mode",
        str(options.mode),
    ]
    if options.allow_new_signer:
        command.append("--allow-new-signer")
    result = run(command)
    if not result.ok:
        raise _failure(result)
    return _parse_install(result)


def route(toolkit: Toolkit, handle: Export | None, argv: Sequence[str]) -> None:
    """Forward one script-side command to ``canoe-bootmgr`` unchanged."""
    command: list[str | Path] = [_binary(toolkit)]
    if handle is not None:
        location_flag, location = _location(handle)
        command.extend((location_flag, location))
    elif not any(argument in ("--boot-root", "--source", "--image") for argument in argv):
        command.extend(("--boot-root", toolkit.efisp))
    command.extend(argv)
    result = run(command)
    if result.out:
        emit(result.out.rstrip("\n"))
    if not result.ok:
        raise _failure(result)
