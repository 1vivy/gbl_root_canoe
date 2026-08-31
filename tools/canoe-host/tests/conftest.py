"""Fixtures for the host tools: a real toolkit tree and a mounted boot root."""

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
BOOTMGR_MANIFEST: Final = HOST_ROOT.parent / "canoe-bootmgr" / "Cargo.toml"
BOOTMGR_BINARY: Final = BOOTMGR_MANIFEST.parent / "target" / "debug" / "canoe-bootmgr"
GM2P_BYTES: Final = 120
TZMAP_BYTES: Final = 256
DEFAULT_EFISP_BYTES: Final = 4 * 1024 * 1024

sys.path.insert(0, str(HOST_ROOT))


@dataclass(frozen=True, slots=True)
class FakeDevice:
    """A directory standing in for a mounted persist volume."""

    root: Path

    @property
    def persist(self) -> Path:
        """The persist mount point."""
        return self.root / "persist"

    @property
    def boot_root(self) -> Path:
        """The persist boot root used by local installs."""
        return self.persist / "efisp"

    @property
    def efisp(self) -> Path:
        """The file standing in for the efisp block device."""
        return self.root / "efisp.bin"

    def plant_live_generation(self) -> None:
        """Install a previous generation the next commit has to demote."""
        self.boot_root.mkdir(parents=True, exist_ok=True)
        (self.boot_root / "boot.efi").write_bytes(b"OLD-LIVE")
        (self.boot_root / "boot.efi.gm2p").write_bytes(b"L" * GM2P_BYTES)
        (self.boot_root / "boot.efi.tzmap").write_bytes(b"L" * TZMAP_BYTES)
        (self.boot_root / "boot_backup.efi").write_bytes(b"OLD-BACKUP")


@dataclass(frozen=True, slots=True)
class FakeToolkit:
    """A toolkit directory laid out like the packaged archive."""

    root: Path
    device: FakeDevice
    trace_path: Path

    def run(self, tool: str, *args: str, **env: str) -> subprocess.CompletedProcess[str]:
        """Run a launcher the way an operator does, stubbing only build."""
        environment = dict(os.environ)
        environment.update(CANOE_TRACE=str(self.trace_path))
        environment.update(env)
        if tool == "canoe" and args and args[0] == "build":
            self.stub_build_manager()
        return subprocess.run(
            [sys.executable, str(self.root / tool), *args],
            cwd=self.root,
            capture_output=True,
            text=True,
            check=False,
            env=environment,
        )

    def stub_build_manager(self) -> None:
        """Replace the canonical manager with the build-only fixture stub."""
        target = self.root / "bin" / "canoe-bootmgr"
        shutil.copyfile(STUB_DIR / "canoe-bootmgr", target)
        target.chmod(0o755)

    def trace(self) -> list[str]:
        """The host binaries the run invoked, in order."""
        if not self.trace_path.is_file():
            return []
        return self.trace_path.read_text(encoding="utf-8").splitlines()

    def plant_triplet(self) -> None:
        """Write a valid derived triplet, as a successful build leaves it."""
        efisp = self.root / "efisp"
        efisp.mkdir(parents=True, exist_ok=True)
        (efisp / "boot.efi").write_bytes(b"NEW-LOADER" * 64)
        (efisp / "boot.efi.gm2p").write_bytes(b"G" * GM2P_BYTES)
        (efisp / "boot.efi.tzmap").write_bytes(b"T" * TZMAP_BYTES)

    def read(self, relative: str) -> bytes:
        """Read a file inside the toolkit."""
        return (self.root / relative).read_bytes()


def _build_device(root: Path, *, efisp_bytes: int) -> FakeDevice:
    device = FakeDevice(root)
    (device.persist / "efisp" / "tools").mkdir(parents=True, exist_ok=True)
    (root / "tmp").mkdir(parents=True, exist_ok=True)
    (root / "vendor_persist").mkdir(parents=True, exist_ok=True)
    device.efisp.write_bytes(b"MZOLDBDS".ljust(efisp_bytes, b"\0"))
    return device


def _bootmgr_source() -> Path:
    """Build the canonical transaction binary once for host integration tests."""
    if not BOOTMGR_BINARY.is_file():
        result = subprocess.run(
            ["cargo", "build", "--manifest-path", str(BOOTMGR_MANIFEST)],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            raise RuntimeError(f"cargo build canoe-bootmgr failed:\n{result.stderr}")
    return BOOTMGR_BINARY


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
    bootmgr = root / "bin" / "canoe-bootmgr"
    shutil.copyfile(_bootmgr_source(), bootmgr)
    bootmgr.chmod(0o755)
    (root / "images").mkdir()
    tools = root / "efisp" / "tools"
    tools.mkdir(parents=True)
    for name in ("ArbTools.efi", "BLTools.efi", "RebootTools.efi"):
        (tools / name).write_text(f"TOOL:{name}\n", encoding="utf-8")
    (root / "BDS.efi").write_bytes(b"MZ-NEW-BDS-IMAGE" * 32)
    return FakeToolkit(root, device, trace)


class ToolkitFactory(Protocol):
    """Builds an independent fixture toolkit plus its mounted device."""

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
    """A toolkit whose mounted device already carries a live generation."""
    return make_toolkit()
