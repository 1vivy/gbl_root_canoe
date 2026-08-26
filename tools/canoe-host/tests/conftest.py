"""Fixtures for the canoe host tools: a real toolkit tree and a fake device.

Nothing here mocks the tools. A fixture toolkit is a real directory laid out
exactly as the packaged archive is - launchers and the `canoe` package in the
root, stand-in binaries in `bin/`, the real device-side transaction script - and
the tests drive it by running the launcher, the way an operator does. Only the
leaves are stand-ins: the six host binaries and adb itself, because a test
cannot patch an ABL or enumerate a phone.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Final, Protocol

import pytest

HOST_ROOT: Final = Path(__file__).resolve().parent.parent
REPO_ROOT: Final = HOST_ROOT.parent.parent
STUB_DIR: Final = Path(__file__).resolve().parent / "stubs"
LAUNCHERS: Final = ("canoe",)
HOST_BINARIES: Final = (
    "extractfv",
    "patch_abl",
    "mode2_profile",
    "abl_tzmap",
    "vbmetabackup",
    "vbmetaport",
)
GM2P_BYTES: Final = 120
TZMAP_BYTES: Final = 256
DEFAULT_EFISP_BYTES: Final = 4 * 1024 * 1024

sys.path.insert(0, str(HOST_ROOT))


@dataclass(frozen=True, slots=True)
class FakeDevice:
    """A directory standing in for the device, as stub_adb.py remaps it."""

    root: Path

    @property
    def persist(self) -> Path:
        """The persist mount point."""
        return self.root / "persist"

    @property
    def boot_root(self) -> Path:
        """The persist boot root the transaction installs into."""
        return self.persist / "efisp"

    @property
    def efisp(self) -> Path:
        """The file standing in for the efisp block device."""
        return self.root / "efisp.bin"

    @property
    def log(self) -> str:
        """Everything adb was asked to do, in order."""
        path = self.root / "adb.log"
        return path.read_text(encoding="utf-8") if path.is_file() else ""

    def saw(self, needle: str) -> bool:
        """True when the device was asked to do something containing `needle`."""
        return needle in self.log

    def set_slot(self, suffix: str) -> None:
        """Report `suffix` as the active slot (empty means non-A/B)."""
        (self.root / "slot_suffix").write_text(suffix, encoding="utf-8")
        cmdline = f"androidboot.slot_suffix={suffix} rootwait\n" if suffix else "rootwait\n"
        (self.root / "cmdline").write_text(cmdline, encoding="utf-8")

    def set_partition(self, name: str, payload: bytes) -> None:
        """Give a by-name partition its contents."""
        (self.root / f"{name}.bin").write_bytes(payload)

    def plant_live_generation(self) -> None:
        """Install a previous generation the next commit has to demote."""
        self.boot_root.mkdir(parents=True, exist_ok=True)
        (self.boot_root / "boot.efi").write_bytes(b"OLD-LIVE")
        (self.boot_root / "boot.efi.gm2p").write_bytes(b"L" * GM2P_BYTES)
        (self.boot_root / "boot.efi.tzmap").write_bytes(b"L" * TZMAP_BYTES)
        (self.boot_root / "boot_backup.efi").write_bytes(b"OLD-BACKUP")


@dataclass(frozen=True, slots=True)
class FakeToolkit:
    """A toolkit directory laid out like the packaged archive, plus its device."""

    root: Path
    device: FakeDevice
    trace_path: Path

    def run(self, tool: str, *args: str, **env: str) -> subprocess.CompletedProcess[str]:
        """Run a launcher the way an operator does, from the toolkit directory."""
        environment = dict(os.environ)
        environment.update(
            STUB_DEV=str(self.device.root),
            CANOE_TRACE=str(self.trace_path),
            CANOE_ADB_WAIT="1",
        )
        environment.update(env)
        return subprocess.run(
            [sys.executable, str(self.root / tool), *args],
            cwd=self.root,
            capture_output=True,
            text=True,
            check=False,
            env=environment,
        )

    def trace(self) -> list[str]:
        """The host binaries the run invoked, in order."""
        if not self.trace_path.is_file():
            return []
        return self.trace_path.read_text(encoding="utf-8").splitlines()
    @property
    def slot_receipt(self) -> Path:
        """The source slot receipt produced by prep-device."""
        return self.root / ".canoe-source-slot"

    def plant_triplet(self) -> None:
        """Write a valid derived triplet, as a successful `canoe build` leaves it."""
        efisp = self.root / "efisp"
        efisp.mkdir(parents=True, exist_ok=True)
        (efisp / "boot.efi").write_bytes(b"NEW-LOADER" * 64)
        (efisp / "boot.efi.gm2p").write_bytes(b"G" * GM2P_BYTES)
        (efisp / "boot.efi.tzmap").write_bytes(b"T" * TZMAP_BYTES)

    def read(self, relative: str) -> bytes:
        """Bytes of a file inside the toolkit."""
        return (self.root / relative).read_bytes()


def _build_device(root: Path, *, efisp_bytes: int) -> FakeDevice:
    device = FakeDevice(root)
    for directory in (device.persist / "efisp" / "tools", root / "tmp", root / "vendor_persist"):
        directory.mkdir(parents=True, exist_ok=True)
    (root / "proc_mounts").write_text(
        f"/dev/block/by-name/persist {device.persist} ext4 rw 0 0\n", encoding="utf-8"
    )
    device.set_slot("_a")
    device.set_partition("abl_a", b"STOCK-ABL-IMAGE")
    device.set_partition("vbmeta_a", b"STOCK-VBMETA-IMAGE")
    device.efisp.write_bytes(b"MZOLDBDS".ljust(efisp_bytes, b"\0"))
    return device


def _build_toolkit(root: Path, device: FakeDevice, trace: Path) -> FakeToolkit:
    root.mkdir(parents=True, exist_ok=True)
    shutil.copytree(HOST_ROOT / "canoelib", root / "canoelib")
    for launcher in LAUNCHERS:
        target = root / launcher
        shutil.copyfile(HOST_ROOT / launcher, target)
        target.chmod(0o755)
    (root / "bin").mkdir()
    for binary in HOST_BINARIES:
        target = root / "bin" / binary
        shutil.copyfile(STUB_DIR / binary, target)
        target.chmod(0o755)
    (root / "Platform-Tools").mkdir()
    adb = root / "Platform-Tools" / "adb"
    shutil.copyfile(Path(__file__).resolve().parent / "stub_adb.py", adb)
    adb.chmod(0o755)
    (root / "images").mkdir()
    tools = root / "efisp" / "tools"
    tools.mkdir(parents=True)
    for name in ("ArbTools.efi", "BLTools.efi", "RebootTools.efi"):
        (tools / name).write_text(f"TOOL:{name}\n", encoding="utf-8")
    (root / "BDS.efi").write_bytes(b"MZ-NEW-BDS-IMAGE" * 32)
    install = root / "canoe_device_install.sh"
    shutil.copyfile(REPO_ROOT / "tools" / "canoe-device" / "canoe_device_install.sh", install)
    install.chmod(0o755)
    boot_entry = root / "canoe_boot_entry.sh"
    shutil.copyfile(REPO_ROOT / "tools" / "canoe-device" / "canoe_boot_entry.sh", boot_entry)
    boot_entry.chmod(0o755)
    return FakeToolkit(root, device, trace)


class ToolkitFactory(Protocol):
    """Builds an independent fixture toolkit plus its device."""

    def __call__(self, *, live: bool = ..., efisp_bytes: int = ...) -> FakeToolkit: ...


@pytest.fixture
def make_toolkit(tmp_path: Path) -> ToolkitFactory:
    """Factory for fixture toolkits: `make_toolkit(live=False, efisp_bytes=...)`."""
    counter = 0

    def factory(*, live: bool = True, efisp_bytes: int = DEFAULT_EFISP_BYTES) -> FakeToolkit:
        nonlocal counter
        counter += 1
        base = tmp_path / f"case{counter}"
        device = _build_device(base / "dev", efisp_bytes=efisp_bytes)
        if live:
            device.plant_live_generation()
        return _build_toolkit(base / "tk", device, base / "trace.log")

    return factory


@pytest.fixture
def toolkit(make_toolkit: ToolkitFactory) -> FakeToolkit:
    """A toolkit whose device already carries a live generation."""
    return make_toolkit()
