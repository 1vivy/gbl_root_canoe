"""Reproduce synthetic v3 vendor_boot fixtures; no device data is included."""
import ctypes
import gzip
import struct
from pathlib import Path


def entry(name, data):
    name = name.encode() + b'\0'
    fields = [1, 0o100644, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(name), 0]
    result = b'070701' + b''.join(f'{field:08x}'.encode() for field in fields) + name
    result += b'\0' * (-len(result) % 4)
    result += data
    return result + b'\0' * (-len(result) % 4)


def fixture(ramdisk):
    image = bytearray(16_384)
    image[:8] = b'VNDRBOOT'
    for offset, value in [(8, 3), (12, 4096), (24, len(ramdisk)), (2096, 2112), (2100, 8)]:
        struct.pack_into('<I', image, offset, value)
    image[28:39] = b'console=tty'
    image[4096:4096 + len(ramdisk)] = ramdisk
    image[8192:8200] = b'DTB-kept'
    image[-64:] = bytes(range(64))
    return image


cpio = entry('lib/modules/modules.load', b'oplus_secure_guard_new.ko\nother.ko\n')
cpio += entry('lib/modules/modules.blocklist', b'blocklist other\n')
cpio += entry('TRAILER!!!', b'')
lz4 = ctypes.CDLL('liblz4.so.1')
lz4.LZ4_compress_default.argtypes = [ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
lz4.LZ4_compress_default.restype = ctypes.c_int
compressed = ctypes.create_string_buffer(len(cpio) + len(cpio) // 255 + 16)
count = lz4.LZ4_compress_default(cpio, compressed, len(cpio), len(compressed))
assert count > 0
legacy_lz4 = bytes.fromhex('02214c18') + struct.pack('<I', count) + compressed.raw[:count]
root = Path(__file__).parent
for name, data in [('cpio', cpio), ('gzip', gzip.compress(cpio, mtime=0)), ('lz4', legacy_lz4)]:
    (root / f'vendorboot-{name}.bin').write_bytes(fixture(data))
