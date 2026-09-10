#!/usr/bin/env python3
"""Independent e2fsprogs fixtures; only disposable images are modified.

CANOE_EXT4_CAPTURE may name one existing capture for additional read-only
qualification. No captures, UUIDs, or device contents enter the repository.
"""
import ctypes
import ctypes.util
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def run(*args):
    subprocess.run(args, check=True)


lib = ctypes.CDLL(ctypes.util.find_library("ext2fs") or "libext2fs.so.2")
lib.ext2fs_open.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int,
                           ctypes.c_uint, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
lib.ext2fs_open.restype = ctypes.c_long
lib.ext2fs_group_desc_csum_verify.argtypes = [ctypes.c_void_p, ctypes.c_uint]
lib.ext2fs_group_desc_csum_verify.restype = ctypes.c_int
lib.ext2fs_close.argtypes = [ctypes.c_void_p]
io = ctypes.c_void_p.in_dll(lib, "unix_io_manager")


def geometry(path):
    with path.open("rb") as image:
        image.seek(1024)
        sb = image.read(1024)
    block = 1024 << struct.unpack_from("<I", sb, 24)[0]
    blocks, first, per_group = (struct.unpack_from("<I", sb, offset)[0]
                                for offset in (4, 20, 32))
    incompat = struct.unpack_from("<I", sb, 96)[0]
    size = struct.unpack_from("<H", sb, 254)[0] if incompat & 0x80 else 32
    return block, size, (blocks - first + per_group - 1) // per_group


def independent_verify(path, corrupt=False):
    fs = ctypes.c_void_p()
    assert lib.ext2fs_open(os.fsencode(path), 0, 0, 0, io, ctypes.byref(fs)) == 0
    try:
        for group in range(geometry(path)[2]):
            assert bool(lib.ext2fs_group_desc_csum_verify(fs, group)) == (not corrupt or group != 0)
    finally:
        lib.ext2fs_close(fs)


with tempfile.TemporaryDirectory(prefix="canoe-ext4-checksums-") as directory:
    root = Path(directory)
    # Include partial groups, the Android-style 128 MiB geometry, 64-byte GDTs,
    # and metadata_csum so the separate CRC32C path is not accidentally changed.
    for mib, checksum, wide in [(46, "uninit_bg", False), (48, "uninit_bg", True),
                                (128, "uninit_bg", False), (160, "uninit_bg", True),
                                (48, "metadata_csum", True)]:
        image = root / f"{mib}-{checksum}-{wide}.img"
        with image.open("wb") as f:
            f.truncate(mib * 1024 * 1024)
        features = ("none,has_journal,ext_attr,dir_index,filetype,extent,sparse_super,"
                    "large_file,huge_file,dir_nlink,extra_isize," + checksum +
                    (",64bit" if wide else ""))
        run("mke2fs", "-q", "-F", "-t", "ext4", "-b", "4096", "-I", "256",
            "-O", features, str(image))
        independent_verify(image)
        run("./test_ext4_checksums", str(image), "accept")
        if checksum == "uninit_bg":
            # The read-only driver's preexisting RECOVER acceptance is kept.
            with image.open("r+b") as f:
                f.seek(1024 + 96)
                flags = struct.unpack("<I", f.read(4))[0]
                f.seek(1024 + 96); f.write(struct.pack("<I", flags | 4))
            independent_verify(image)
            run("./test_ext4_checksums", str(image), "accept")
        for field in (12, 30):  # Descriptor contents and checksum itself.
            bad = root / "corrupt.img"
            shutil.copyfile(image, bad)
            block, _, _ = geometry(bad)
            with bad.open("r+b") as f:
                offset = block + field
                f.seek(offset); value = f.read(1)
                f.seek(offset); f.write(bytes([value[0] ^ 1]))
            independent_verify(bad, corrupt=True)
            run("./test_ext4_checksums", str(bad), "reject")
    if capture := os.environ.get("CANOE_EXT4_CAPTURE"):
        capture = Path(capture).resolve()
        independent_verify(capture)
        run("./test_ext4_checksums", str(capture), "accept")
print("PASS ext4 CRC fixtures: e2fsprogs agrees, corrupt descriptors fail, small persist/RECOVER preserved")
