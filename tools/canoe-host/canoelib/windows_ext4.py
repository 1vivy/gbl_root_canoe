"""Fetch the Windows ext4 bridge without vendoring third-party binaries."""

from __future__ import annotations

import hashlib
import os
import urllib.error
import urllib.request
from pathlib import Path
from typing import Final

from .errors import CanoeError

WINFSP_URL: Final = "https://github.com/winfsp/winfsp/releases/download/v2.1/winfsp-2.1.25156.msi"
WINFSP_SHA256: Final = "073a70e00f77423e34bed98b86e600def93393ba5822204fac57a29324db9f7a"
LKL_FUSE_URL: Final = (
    "https://github.com/lsds/lkl/archive/8a1fc6cf60d853e9abf724a3ed27d5680fb5807f.tar.gz"
)
LKL_FUSE_SHA256: Final = "71abdcb94234fbb0b3f32eaaaf30fa8c4e9c0ac5055b60e04f21b01521afd908"


def _cache_root() -> Path:
    root = os.environ.get("LOCALAPPDATA")
    if not root:
        raise CanoeError("LOCALAPPDATA is unavailable; cannot cache Windows ext4 tools")
    return Path(root) / "Canoe" / "ext4"


def _download(url: str, expected: str, destination: Path, label: str) -> None:
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    try:
        with urllib.request.urlopen(url, timeout=30) as response, temporary.open("wb") as handle:
            while chunk := response.read(1024 * 1024):
                handle.write(chunk)
    except (OSError, urllib.error.URLError) as exc:
        temporary.unlink(missing_ok=True)
        raise CanoeError(f"could not fetch {label}: {exc}") from exc
    digest = hashlib.sha256(temporary.read_bytes()).hexdigest()
    if digest != expected:
        temporary.unlink(missing_ok=True)
        raise CanoeError(
            f"{label} SHA-256 verification failed; expected {expected}, got {digest}. "
            "Delete the cache and retry from the official release source."
        )
    temporary.replace(destination)


def ensure() -> tuple[Path, Path]:
    """Fetch and verify WinFsp and the pinned LKL/lklfuse source on first use."""
    root = _cache_root()
    try:
        root.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        raise CanoeError(f"could not create Windows ext4 cache {root}: {exc}") from exc
    winfsp = root / "winfsp-2.1.25156.msi"
    lkl = root / "lklfuse-8a1fc6cf.tar.gz"
    if not winfsp.is_file() or hashlib.sha256(winfsp.read_bytes()).hexdigest() != WINFSP_SHA256:
        _download(WINFSP_URL, WINFSP_SHA256, winfsp, "WinFsp")
    if not lkl.is_file() or hashlib.sha256(lkl.read_bytes()).hexdigest() != LKL_FUSE_SHA256:
        _download(LKL_FUSE_URL, LKL_FUSE_SHA256, lkl, "LKL lklfuse")
    return winfsp, lkl
