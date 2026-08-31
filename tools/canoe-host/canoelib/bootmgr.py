"""Thin process adapter for the canonical ``canoe-bootmgr`` binary."""

from __future__ import annotations

import json
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path, PurePath
from typing import Final, Literal, Protocol, TypeAlias

from .errors import CanoeError
from .layout import Toolkit
from .proc import Completed, run
from .ui import emit


class ExportLike(Protocol):
    """Structural view of a host source used by boot-manager routing."""

    @property
    def backend(self) -> Literal["local", "ext4"]: ...

    @property
    def boot_root(self) -> Path | PurePath | None: ...

    @property
    def source(self) -> Path | PurePath | None: ...


SourceKind = Literal["block", "image", "dir"]

__all__: Final = (
    "InstallOptions",
    "InstallReceipt",
    "SourceCandidate",
    "detect",
    "install",
    "route",
)

JsonValue: TypeAlias = (
    str | int | float | bool | list["JsonValue"] | dict[str, "JsonValue"] | None
)


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


@dataclass(frozen=True, slots=True)
class SourceCandidate:
    """One source returned by the canonical source detector."""

    kind: SourceKind
    path: str
    identity: str | None
    model: str | None
    size_bytes: int
    boot_root: str | None
    boot_root_present: bool
    readable: bool
    writable: bool
    needs_privilege: bool
    mounted_at: str | None
    why: str


def _location(handle: ExportLike) -> tuple[str, str]:
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
    try:
        return toolkit.tool("canoe-bootmgr")
    except CanoeError as exc:
        raise CanoeError(
            "canoe-bootmgr is required; build it with "
            "`cargo build --release --locked --manifest-path tools/canoe-bootmgr/Cargo.toml` "
            "or install it in the toolkit bin directory"
        ) from exc


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
def _optional_text(value: JsonValue, field: str) -> str | None:
    """Parse a nullable text field from a detector response."""
    if value is None:
        return None
    if not isinstance(value, str):
        raise CanoeError(f"canoe-bootmgr response has invalid {field}")
    return value


def _parse_source(value: JsonValue, index: int) -> SourceCandidate:
    """Parse one detector row at the process boundary."""
    if not isinstance(value, dict):
        raise CanoeError(f"canoe-bootmgr source response row {index} is not an object")
    raw_kind = value.get("kind")
    if raw_kind not in ("block", "image", "dir"):
        raise CanoeError(f"canoe-bootmgr source response row {index} has invalid kind")
    path = value.get("path")
    why = value.get("why")
    if not isinstance(path, str) or not path:
        raise CanoeError(f"canoe-bootmgr source response row {index} has invalid path")
    if not isinstance(why, str):
        raise CanoeError(f"canoe-bootmgr source response row {index} has invalid why")
    return SourceCandidate(
        kind=raw_kind,
        path=path,
        identity=_optional_text(value.get("identity"), "identity"),
        model=_optional_text(value.get("model"), "model"),
        size_bytes=_integer(value.get("size_bytes"), "size_bytes"),
        boot_root=_optional_text(value.get("boot_root"), "boot_root"),
        boot_root_present=_boolean(value.get("boot_root_present"), "boot_root_present"),
        readable=_boolean(value.get("readable"), "readable"),
        writable=_boolean(value.get("writable"), "writable"),
        needs_privilege=_boolean(value.get("needs_privilege"), "needs_privilege"),
        mounted_at=_optional_text(value.get("mounted_at"), "mounted_at"),
        why=why,
    )


def _parse_detect(result: Completed) -> tuple[SourceCandidate, ...]:
    """Parse the canonical source detector response."""
    try:
        payload = json.loads(result.out)
    except json.JSONDecodeError as exc:
        raise CanoeError(f"canoe-bootmgr returned invalid JSON: {exc}") from exc
    if not isinstance(payload, dict) or payload.get("ok") is not True:
        raise CanoeError("canoe-bootmgr returned an unsuccessful source response")
    if payload.get("kind") != "source.detect":
        raise CanoeError("canoe-bootmgr returned an unexpected source response")
    sources = payload.get("sources")
    if not isinstance(sources, list):
        raise CanoeError("canoe-bootmgr source response has no sources list")
    return tuple(_parse_source(value, index) for index, value in enumerate(sources))


def detect(toolkit: Toolkit) -> tuple[SourceCandidate, ...]:
    """Enumerate host sources through the canonical boot-manager detector."""
    result = run([_binary(toolkit), "source", "detect", "--json"])
    if not result.ok:
        raise _failure(result)
    return _parse_detect(result)


def patch_vendor_boot(toolkit: Toolkit, image: Path, out: Path) -> None:
    """Amend a vendor_boot command line through the canonical patcher."""
    out.parent.mkdir(parents=True, exist_ok=True)
    result = run([_binary(toolkit), "--json", "vendor-boot", "patch", image, out])
    if not result.ok:
        raise _failure(result)


def install(
    toolkit: Toolkit,
    handle: ExportLike,
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


def _has_backend_argument(argv: Sequence[str]) -> bool:
    """Detect a global backend option without mistaking entry.set --image for one."""
    backend_flags = ("--boot-root", "--source", "--ext4-image")
    return any(argument.split("=", maxsplit=1)[0] in backend_flags for argument in argv)


def route(toolkit: Toolkit, handle: ExportLike | None, argv: Sequence[str]) -> None:
    """Forward one script-side command to ``canoe-bootmgr`` unchanged."""
    command: list[str | Path] = [_binary(toolkit)]
    if handle is not None:
        location_flag, location = _location(handle)
        command.extend((location_flag, location))
    elif not _has_backend_argument(argv):
        command.extend(("--boot-root", toolkit.efisp))
    command.extend(argv)
    result = run(command)
    if result.out:
        emit(result.out.rstrip("\n"))
    if not result.ok:
        raise _failure(result)
