"""Where everything lives inside a toolkit directory, and the exact sizes.

The sidecar lengths are contract values, not sanity checks: the BDS reads a
120-byte KeyMint profile and a 256-byte TrustZone map at fixed offsets, so a
wrong length is a boot-time failure, and the tools refuse to ship one.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from .errors import CanoeError

GM2P_BYTES: Final = 120
TZMAP_BYTES: Final = 256



def platform_names(name: str) -> tuple[str, ...]:
    """Candidate file names for a host executable, this platform's first."""
    return (f"{name}.exe", name) if os.name == "nt" else (name, f"{name}.exe")


def size_of(path: Path) -> int:
    """Byte length of `path`, or 0 when it is not a file."""
    return path.stat().st_size if path.is_file() else 0


def require_nonempty(path: Path, message: str) -> int:
    """Return the size of `path`, failing with `message` if it is missing or empty."""
    size = size_of(path)
    if size == 0:
        raise CanoeError(message)
    return size


def require_exact(path: Path, want: int, label: str) -> None:
    """Fail unless `path` is exactly `want` bytes long."""
    got = size_of(path)
    if got != want:
        raise CanoeError(f"{label} must be exactly {want} bytes, got {got}")


@dataclass(frozen=True, slots=True)
class Toolkit:
    """One toolkit directory: the binaries in bin/ and the artifacts around them."""

    root: Path

    @classmethod
    def shipped(cls) -> Toolkit:
        """The toolkit this package is installed in: the directory above it.

        True of the packaged archives and of the test fixtures alike, because
        both put the launchers and the `canoe` package in the toolkit root.
        """
        return cls(Path(__file__).resolve().parent.parent)

    @property
    def bin(self) -> Path:
        """Directory holding the host binaries shipped with the toolkit."""
        return self.root / "bin"

    @property
    def efisp(self) -> Path:
        """Staging copy of the persist boot root."""
        return self.root / "efisp"

    @property
    def boot_efi(self) -> Path:
        """The patched ABL loader."""
        return self.efisp / "boot.efi"

    @property
    def gm2p(self) -> Path:
        """The KeyMint profile sidecar for `boot_efi`."""
        return self.efisp / "boot.efi.gm2p"

    @property
    def tzmap(self) -> Path:
        """The TrustZone interface map sidecar for `boot_efi`."""
        return self.efisp / "boot.efi.tzmap"

    @property
    def triplet(self) -> tuple[Path, Path, Path]:
        """The loader and both sidecars, which only ever ship or fail together."""
        return (self.boot_efi, self.gm2p, self.tzmap)

    @property
    def bootentries(self) -> Path:
        """The BDS boot entry list."""
        return self.efisp / "BOOTENTRIES"
    @property
    def canoe_cfg(self) -> Path:
        """Declarative menu state installed beside the boot entry files."""
        return self.efisp / "canoe.cfg"

    @property
    def efisp_tools(self) -> Path:
        """The tools submenu shipped under the boot root."""
        return self.efisp / "tools"

    @property
    def bds(self) -> Path:
        """The superfastboot BDS, written raw to the efisp partition."""
        return self.root / "BDS.efi"

    @property
    def images(self) -> Path:
        """Directory holding the abl/vbmeta pair the build derives from."""
        return self.root / "images"

    @property
    def abl_image(self) -> Path:
        """Source ABL image."""
        return self.images / "abl.img"

    @property
    def vbmeta_image(self) -> Path:
        """Source vbmeta image, which must match `abl_image`."""
        return self.images / "vbmeta.img"

    @property
    def abl_original(self) -> Path:
        """The unpatched loader lifted out of the ABL image."""
        return self.root / "ABL_original.efi"

    @property
    def patch_log(self) -> Path:
        """Combined output of the ABL patcher."""
        return self.root / "patch_log.txt"

    @property
    def device_install(self) -> Path:
        """The device-side transaction script staged and invoked by `canoe install`."""
        return self.root / "canoe_device_install.sh"

    def tool(self, name: str) -> Path:
        """The bin/ binary called `name`, with whatever extension this host uses."""
        for candidate in platform_names(name):
            path = self.bin / candidate
            if path.is_file():
                return path
        raise CanoeError(f"missing bin/{name}")
