"""Mount a BDS-exported persist volume for the host install transaction."""

from __future__ import annotations

import math
import os
import shutil
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path, PurePath
from typing import Final, Literal

from . import proc
from .errors import CanoeError
from .layout import platform_names
from .massstorage_windows import export as export_windows
from .ui import note, warn

__all__: Final = ("Export", "export", "fastboot_binary", "local_boot_root", "release")

MountKind = Literal["fuse", "system", "windows"]



@dataclass(frozen=True, slots=True)
class Export:
    """Describe the mounted boot root and lifecycle ownership."""

    boot_root: Path | PurePath
    mount: Path | PurePath
    node: Path | PurePath | None
    owned: bool
    kind: MountKind | None = None
    mounter: Path | None = None
    drive: str | None = None


_SYS_BLOCK: Final = Path("/sys/block")
_DEV_ROOT: Final = Path("/dev")
_USB_MODESWITCH_OVERRIDE: Final = Path("/etc/usb_modeswitch.d")
_MSC_GADGET_ID: Final = "05c6:f000"
# The BDS rewrites the resident export driver's presentation to this canoe
# identity before the session starts (fixed-disk INQUIRY, pid.codes VID,
# product "efisp boot root"). 05c6:f000 remains accepted because an
# unfamiliar firmware layout degrades to the stock presentation.
_MSC_CANOE_GADGET_ID: Final = "1209:ca0e"
_MSC_GADGET_IDS: Final = frozenset({_MSC_GADGET_ID, _MSC_CANOE_GADGET_ID})
_MOUNTINFO: Final = Path("/proc/self/mountinfo")
# `<id> <parent> <maj:min> <root> <point> <opts> [tags] - <fstype> <source> <opts>`
_MOUNTINFO_POINT: Final = 4
_MOUNTINFO_SOURCE: Final = 1
_MOUNTINFO_ESCAPES: Final = {"040": " ", "011": "\t", "012": "\n", "134": "\\"}


def _write_probe(directory: Path, description: str) -> None:
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", prefix=".canoe-write-", dir=directory, delete=True
        ):
            pass
    except OSError as exc:
        raise CanoeError(f"{description} is not writable: {exc}") from exc


def _ensure_boot_root(mount: Path) -> Path:
    boot_root = mount / "efisp"
    if boot_root.exists() and not boot_root.is_dir():
        raise CanoeError(f"boot root is not a directory: {boot_root}")
    if not boot_root.exists():
        try:
            boot_root.mkdir()
        except OSError as exc:
            raise CanoeError(f"could not create boot root {boot_root}: {exc}") from exc
    _write_probe(boot_root, f"boot root {boot_root}")
    return boot_root


def local_boot_root(path: Path) -> Export:
    """Adopt an already-mounted persist filesystem without taking ownership."""
    if not path.is_dir():
        raise CanoeError(f"persist mount is not a directory: {path}")
    _write_probe(path, f"persist mount {path}")
    if path.name == "efisp":
        mount, boot_root = path.parent, path
    else:
        mount, boot_root = path, _ensure_boot_root(path)
    return Export(boot_root=boot_root, mount=mount, node=None, owned=False)


def _usb_gadget(device: Path) -> Path | None:
    """Return the USB device node behind a sysfs block device, if any.

    `/sys/block/<name>/device` is a symlink into `/sys/devices`, so the USB
    ancestry exists only along the *resolved* path: the lexical parents are
    `/sys/block/<name>`, `/sys/block` and `/sys`, and none of those is ever a
    usb node. Measured on the BDS export, the link resolves to
    `.../usb8/8-2/8-2.2/8-2.2:1.0/host12/target12:0:0/12:0:0:0`, whose own
    subsystem is `scsi`; the identity sits four levels up on `8-2.2`. Walking
    the unresolved link is why discovery matched nothing and timed out on every
    run even while the LUN was present and mountable.

    The interface node `8-2.2:1.0` is in the usb subsystem too but carries no
    `idVendor`, so the walk continues up to the device that does.
    """
    try:
        resolved = device.resolve(strict=True)
    except OSError:
        return None
    for node in (resolved, *resolved.parents):
        if node.name == "sys" or node == Path(node.root):
            break
        try:
            subsystem = (node / "subsystem").resolve(strict=True)
        except OSError:
            continue
        if subsystem.name == "usb" and (node / "idVendor").is_file():
            return node
    return None


def _gadget_id(gadget: Path) -> str | None:
    """Read a USB device's `vid:pid` in the lowercase hex sysfs spelling."""
    try:
        vendor = (gadget / "idVendor").read_text(encoding="utf-8").strip()
        product = (gadget / "idProduct").read_text(encoding="utf-8").strip()
    except OSError:
        return None
    return f"{vendor.lower()}:{product.lower()}"


def _usb_disks() -> dict[str, str | None]:
    """Map every USB-backed whole disk to the gadget id behind it."""
    try:
        entries = sorted(_SYS_BLOCK.iterdir())
    except OSError as exc:
        raise CanoeError(f"could not inspect {_SYS_BLOCK}: {exc}") from exc
    disks: dict[str, str | None] = {}
    for entry in entries:
        if not entry.is_dir():
            continue
        gadget = _usb_gadget(entry / "device")
        if gadget is not None:
            disks[entry.name] = _gadget_id(gadget)
    return disks


def _exported_disks(disks: dict[str, str | None]) -> tuple[str, ...]:
    """Names of the disks whose USB identity is the BDS export gadget."""
    return tuple(name for name, gadget in disks.items() if gadget in _MSC_GADGET_IDS)


def _unescape_mountinfo(field: str) -> str:
    """Decode the escapes the kernel writes into /proc/*/mountinfo.

    `mangle_path()` escapes exactly space, tab, newline and backslash, each as
    an octal triple; every other byte is written verbatim.
    """
    out: list[str] = []
    index = 0
    while index < len(field):
        escape = (
            _MOUNTINFO_ESCAPES.get(field[index + 1 : index + 4]) if field[index] == "\\" else None
        )
        if escape is None:
            out.append(field[index])
            index += 1
            continue
        out.append(escape)
        index += 4
    return "".join(out)


def _mounts_of(node: Path) -> tuple[Path, ...]:
    """Mount points the kernel currently lists for one block device."""
    try:
        text = _MOUNTINFO.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        raise CanoeError(f"could not read {_MOUNTINFO}: {exc}") from exc
    wanted = str(node)
    mounts: list[Path] = []
    for line in text.splitlines():
        head, separator, tail = line.partition(" - ")
        if not separator:
            continue
        fields, source = head.split(" ", _MOUNTINFO_POINT + 1), tail.split(" ", 2)
        if len(fields) <= _MOUNTINFO_POINT or len(source) <= _MOUNTINFO_SOURCE:
            continue
        if _unescape_mountinfo(source[_MOUNTINFO_SOURCE]) != wanted:
            continue
        mounts.append(Path(_unescape_mountinfo(fields[_MOUNTINFO_POINT])))
    return tuple(mounts)


def _detach_commands(node: Path, mount: Path) -> tuple[list[str | Path], ...]:
    """Unmounters to try, best first: udisksctl also reaps the dir it created."""
    commands: list[list[str | Path]] = []
    udisksctl = shutil.which("udisksctl")
    if udisksctl is not None:
        commands.append([udisksctl, "unmount", "-b", node])
    umount, sudo = shutil.which("umount"), shutil.which("sudo")
    if umount is not None:
        commands.append([sudo, umount, mount] if sudo is not None else [umount, mount])
    return tuple(commands)


def _detach_automounts(node: Path) -> None:
    """Take the export off the desktop automounter before canoe mounts it.

    udisks mounts the LUN the moment it enumerates (measured: /dev/sda at
    /run/media/<user>/<uuid>). That mount would outlive `release()`, so the
    operator's Volume Down would then pull the LUN out from under a live ext4
    carrying unflushed journal state. canoe owns the export lifecycle, so the
    stranger's copy goes first; only mounts whose source is this exact node are
    ever touched.
    """
    for mount in _mounts_of(node):
        note(f"Releasing the automounted export at {mount}")
        commands = _detach_commands(node, mount)
        if not commands:
            raise CanoeError(f"{node} is mounted at {mount} and no unmounter is available")
        for command in commands:
            if _release_command(command, "unmount") is None:
                break
        else:
            raise CanoeError(
                f"could not unmount the automounted export {node} from {mount};"
                " unmount it by hand and retry"
            )


def _stop_export_process(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is None:
        process.terminate()
        try:
            _ = process.wait(timeout=1.0)
        except subprocess.TimeoutExpired:
            process.kill()
            _ = process.wait()


def _run_mount(command: list[str | Path], node: Path, mount: Path, kind: MountKind) -> None:
    try:
        result = proc.run(command)
    except CanoeError as exc:
        raise CanoeError(f"mount refused for {node} at {mount}: {exc}") from exc
    if result.ok:
        return
    mounter = "fuse2fs" if kind == "fuse" else "sudo mount -t ext4"
    detail = (result.err or result.out).strip()
    raise CanoeError(f"{mounter} refused {node} at {mount}: {detail}")


def _mount(node: Path, mount: Path) -> MountKind:
    """Mount the exported persist, preferring the journaled kernel driver.

    Order matters for data integrity, not convenience. `persist` is a live
    vendor filesystem, and fuse2fs announces that it "does not support using
    the journal. There may be file system corruption or data loss if the file
    system is not gracefully unmounted." Writing a boot root through a
    journal-unaware driver risks the very tree the BDS depends on, so the
    kernel mount wins whenever sudo can provide it.

    The fuse2fs fallback needs `fakeroot`: measured on a root-owned `efisp/`,
    a plain `-o rw` mount refuses every write with EACCES because fuse2fs
    enforces the on-disk owner against the calling uid. `fakeroot` makes the
    fallback usable, at the cost of files landing under the caller's uid
    rather than root.
    """
    mount_binary, sudo = shutil.which("mount"), shutil.which("sudo")
    if mount_binary is not None and sudo is not None:
        _run_mount([sudo, mount_binary, "-t", "ext4", node, mount], node, mount, "system")
        return "system"
    fuse2fs = shutil.which("fuse2fs")
    if fuse2fs is None:
        raise CanoeError("no ext4 mounter available: install fuse2fs or sudo mount")
    warn(
        "Falling back to fuse2fs: it ignores the ext4 journal, so an ungraceful "
        "unmount can corrupt persist, and new files will be owned by this user "
        "rather than root."
    )
    _run_mount([fuse2fs, "-o", "rw,fakeroot", node, mount], node, mount, "fuse")
    return "fuse"


def _mount_export(node: Path) -> Export:
    _detach_automounts(node)
    try:
        mount = Path(tempfile.mkdtemp(prefix="canoe-persist-"))
    except OSError as exc:
        raise CanoeError(f"could not create a mount directory: {exc}") from exc
    kind: MountKind | None = None
    try:
        kind = _mount(node, mount)
        boot_root = _ensure_boot_root(mount)
    except CanoeError as exc:
        shutil.rmtree(mount, ignore_errors=True)
        if kind == "system" and os.geteuid() != 0:
            raise CanoeError(
                "could not prepare boot root on a root-owned ext4 mount for a non-root writer: "
                f"{exc}; re-run canoe as root. Installing fuse2fs is not a substitute: it "
                "ignores the ext4 journal and would write the boot root under your own uid"
            ) from exc
        raise
    return Export(
        boot_root=boot_root,
        mount=mount,
        node=node,
        owned=True,
        kind=kind,
    )


def _stock_identity_hint(disks: dict[str, str | None]) -> str:
    """Name the modeswitch trap only when the stock identity is what showed up.

    The BDS presents the export as the canoe identity, which no udev rule
    claims. A session that came up as Qualcomm's 05c6:f000 means the device
    fell back to its resident driver, and on that identity stock rules match
    a mode-switching modem whose packaged config ejects the disk between
    usb-storage binding and the kernel scan.
    """
    if _MSC_GADGET_ID not in disks.values():
        return ""
    return (
        f" The session came up with the stock {_MSC_GADGET_ID} identity rather than"
        f" {_MSC_CANOE_GADGET_ID}, so the device used its resident driver; stock"
        " usb_modeswitch rules eject that identity mid-scan. Create"
        f" {_USB_MODESWITCH_OVERRIDE / _MSC_GADGET_ID} containing"
        " 'DisableSwitching=1' and retry."
    )


def _find_export(before: frozenset[str], timeout: float) -> Export:
    """Wait for the export LUN, by BDS USB identity first and novelty second.

    Identity comes first so a LUN that was already on the bus, or one that
    re-enumerated under a name seen in `before`, is still found; the novelty
    diff stays as the fallback for a firmware that ships a different gadget id.
    """
    deadline = time.monotonic() + timeout
    while True:
        disks = _usb_disks()
        for name in _exported_disks(disks) or tuple(sorted(set(disks) - before)):
            node = _DEV_ROOT / name
            if node.exists():
                return _mount_export(node)
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            remedy = _stock_identity_hint(disks)
            raise CanoeError(
                "mass-storage export did not expose a new USB SCSI disk within"
                f" {timeout:g}s; the device enumerated but never presented a LUN.{remedy}"
            )
        time.sleep(min(0.1, remaining))


def fastboot_binary(toolkit_root: Path | None = None) -> Path:
    """Resolve the bundled fastboot first, then the operator's PATH copy."""
    root = toolkit_root or Path()
    bundled = tuple(root / "Platform-Tools" / name for name in platform_names("fastboot"))
    for candidate in bundled:
        if candidate.is_file():
            return candidate
    found = shutil.which("fastboot")
    if found is not None:
        return Path(found)
    names = " or ".join(str(path) for path in bundled)
    raise CanoeError(f"fastboot binary not found; expected bundled {names} or fastboot on PATH")




def _windows_export(toolkit_root: Path, target: str, timeout: float, fastboot: Path) -> Export:
    windows = export_windows(toolkit_root, target, timeout, fastboot)
    return Export(
        boot_root=windows.boot_root,
        mount=windows.mount,
        node=windows.node,
        owned=True,
        kind="windows",
        mounter=windows.mounter,
        drive=windows.drive,
    )

def export(toolkit_root: Path, *, target: str = "persist", timeout: float = 60.0) -> Export:
    """Start BDS mass storage and mount its ext4 persist volume."""
    if not math.isfinite(timeout) or timeout < 0:
        raise CanoeError(
            f"mass-storage discovery timeout must be finite and non-negative: {timeout}"
        )
    fastboot = fastboot_binary(toolkit_root)
    if os.name == "nt":
        return _windows_export(toolkit_root, target, timeout, fastboot)
    disks = _usb_disks()
    for name in _exported_disks(disks):
        node = _DEV_ROOT / name
        if node.exists():
            # The BDS is already inside its export loop, where it does not
            # answer fastboot: a second `oem mass-storage:` would block until
            # discovery gave up, and tearing that process down must not disturb
            # the live session. Adopt the LUN already on the bus instead.
            note(f"Adopting the mass-storage export already live at {node}")
            return _mount_export(node)
    try:
        process = subprocess.Popen(
            [str(fastboot), "oem", f"mass-storage:{target}"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except OSError as exc:
        raise CanoeError(f"could not start fastboot mass-storage export: {exc}") from exc
    try:
        return _find_export(frozenset(disks), timeout)
    except CanoeError:
        _stop_export_process(process)
        raise


def _release_command(command: list[str | Path], description: str) -> str | None:
    try:
        result = proc.run(command)
    except CanoeError as exc:
        return f"{description}: {exc}"
    return None if result.ok else f"{description}: {(result.err or result.out).strip()}"


def _unmount_command(handle: Export) -> list[str | Path]:
    mount = Path(str(handle.mount))
    if handle.kind == "fuse":
        fusermount3 = shutil.which("fusermount3")
        if fusermount3 is not None:
            return [fusermount3, "-u", mount]
        umount = shutil.which("umount")
        if umount is not None:
            return [umount, mount]
        raise CanoeError("cannot unmount fuse export: install fusermount3 or umount")
    umount, sudo = shutil.which("umount"), shutil.which("sudo")
    if umount is None or sudo is None:
        raise CanoeError("cannot unmount ext4 export: sudo umount is unavailable")
    return [sudo, umount, mount]

def _release_windows(handle: Export) -> None:
    if handle.mounter is None or handle.drive is None:
        raise CanoeError("Windows export has no ext4windows unmount handle")
    failure = _release_command(
        [handle.mounter, "unmount", handle.drive], "could not unmount Windows persist export"
    )
    if failure is not None:
        raise CanoeError(failure)


def _release_unix(handle: Export) -> None:
    mount = Path(str(handle.mount))
    if not os.path.ismount(mount):
        shutil.rmtree(mount, ignore_errors=True)
        return
    failures: list[str] = []
    failure = _release_command(["sync"], "could not flush the persist mount")
    if failure is not None:
        failures.append(failure)
    try:
        command = _unmount_command(handle)
    except CanoeError as exc:
        failures.append(str(exc))
    else:
        failure = _release_command(command, f"could not unmount persist export {mount}")
        if failure is not None:
            failures.append(failure)
    if failures:
        raise CanoeError("; ".join(failures))
    shutil.rmtree(mount, ignore_errors=True)


def release(handle: Export) -> None:
    """Flush and unmount only; Volume Down on the device ends the BDS session."""
    if not handle.owned:
        return
    if handle.kind == "windows":
        _release_windows(handle)
        return
    _release_unix(handle)
