"""Apply Canoe's kernel-module blacklist to a vendor_boot header in place."""

from __future__ import annotations

import shutil
from pathlib import Path
from typing import Final

from .errors import CanoeError
from .ui import note

MAGIC: Final = b"VNDRBOOT"
CMDLINE_OFFSET: Final = 28
CMDLINE_BYTES: Final = 2048
BLACKLIST: Final = b"module_blacklist=oplus_secure_guard_new"


def patch_cmdline(image: Path, out: Path) -> bool:
    """Copy ``image`` to ``out`` and append the blacklist to its fixed header field."""
    try:
        data = image.read_bytes()
    except OSError as exc:
        raise CanoeError(f"could not read vendor_boot image {image}: {exc}") from exc
    field_end = CMDLINE_OFFSET + CMDLINE_BYTES
    if len(data) < field_end:
        raise CanoeError(f"vendor_boot image is shorter than its cmdline field: {image}")
    if data[: len(MAGIC)] != MAGIC:
        raise CanoeError(f"vendor_boot image has invalid magic (expected VNDRBOOT): {image}")

    field = data[CMDLINE_OFFSET:field_end]
    current = field.split(b"\0", 1)[0]
    if BLACKLIST in current:
        note("vendor_boot already patched")
        patched = data
        changed = False
    else:
        amended = current + b" " + BLACKLIST
        if len(amended) >= CMDLINE_BYTES:
            raise CanoeError("vendor_boot cmdline has no room for the blacklist")
        patched = data[:CMDLINE_OFFSET] + amended.ljust(CMDLINE_BYTES, b"\0") + data[field_end:]
        changed = True

    try:
        out.parent.mkdir(parents=True, exist_ok=True)
        if image.resolve() != out.resolve():
            shutil.copyfile(image, out)
        out.write_bytes(patched)
        reread = out.read_bytes()[CMDLINE_OFFSET:field_end].split(b"\0", 1)[0]
    except OSError as exc:
        raise CanoeError(f"could not write patched vendor_boot image {out}: {exc}") from exc
    if BLACKLIST not in reread:
        raise CanoeError("vendor_boot cmdline patch could not be verified")
    if changed:
        note("vendor_boot patched")
    return changed
