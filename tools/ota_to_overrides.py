#!/usr/bin/env python3
"""
ota_to_overrides.py — generate tools/keymaster_overrides.generated.h from a
fresh OTA payload's vbmeta image.

The QSEECOM hook in tools/qseecom_hook.h injects fixed RoT / BootState bytes
into the SET_ROT (0x201) and SET_BOOT_STATE (0x208) requests it intercepts.
The bytes are computed from the OEM AVB public key the way KeymasterClient.c
hashes them in the GREEN-locked branch, plus OS version / SPL parsed from
vbmeta property descriptors.

Inputs:
    vbmeta.img     — root vbmeta image from a fresh OEM OTA payload.
                     Must be the actual OEM-signed image, not a custom-keyed
                     re-sign; the whole point is to feed TZ the bytes it
                     would have seen if the device booted that OEM image
                     locked.

Output:
    tools/keymaster_overrides.generated.h — gitignored. Picked up
    automatically by tools/keymaster_overrides.h via __has_include.

Run from the repo root:
    python3 tools/ota_to_overrides.py /path/to/vbmeta.img
    make build_hooks_generic     # picks up the generated header

The defaults baked into tools/keymaster_overrides.h are infiniti_glo_703
(OnePlus 15 GLO, Android 16, 2026-04-01 SPL) — fine as a lab fallback but
will produce attestation that doesn't match anything once OEM ships a new
build.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import struct
import sys
from pathlib import Path
from typing import Optional

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT = REPO_ROOT / "tools" / "keymaster_overrides.generated.h"


def _import_avbtool() -> object:
    """avbtool.py is a script, not a packaged module. Try the user's home dir
    (where the project keeps it) plus a few obvious fallbacks. Caller can also
    set AVBTOOL=/path/to/avbtool.py to override."""
    candidates = []
    env = os.environ.get("AVBTOOL")
    if env:
        candidates.append(Path(env))
    candidates += [
        Path.home() / "avbtool.py",
        Path("/usr/bin/avbtool"),
        Path("/usr/local/bin/avbtool"),
        REPO_ROOT / "tools" / "avbtool.py",
    ]
    for path in candidates:
        if path.is_file():
            sys.path.insert(0, str(path.parent))
            mod_name = path.stem
            try:
                return __import__(mod_name)
            except Exception as e:
                print(f"warning: failed to import {path}: {e}", file=sys.stderr)
    raise SystemExit(
        "avbtool.py not found. Set AVBTOOL=/path/to/avbtool.py or place it at "
        "~/avbtool.py / tools/avbtool.py."
    )


def _read_pubkey_blob(vbmeta_path: Path) -> bytes:
    """Pull the raw AVB pubkey blob (the format avb_sha256s in
    KeymasterClient.c) out of vbmeta.img.

    Layout (avbtool.py:580-600):
        [0:256]                                   header
        [256 : 256+auth_size]                     auth block
        [256+auth_size : ...+aux_size]            aux block
            within aux: pubkey at +public_key_offset, length public_key_size
    """
    blob = vbmeta_path.read_bytes()
    if len(blob) < 256:
        raise SystemExit(f"{vbmeta_path}: too small to be a vbmeta image")

    avbtool = _import_avbtool()
    header = avbtool.AvbVBMetaHeader(blob[:256])
    auth_off = 256
    aux_off = auth_off + header.authentication_data_block_size
    pk_off = aux_off + header.public_key_offset
    pk_size = header.public_key_size
    if pk_size == 0:
        raise SystemExit(f"{vbmeta_path}: vbmeta has no public key (unsigned?)")
    if pk_off + pk_size > len(blob):
        raise SystemExit(f"{vbmeta_path}: public key extends past file")
    return blob[pk_off : pk_off + pk_size]


def _parse_props(vbmeta_path: Path) -> dict[str, str]:
    """Walk descriptors and pull out the property descriptors as a flat dict.

    The com.android.build.boot.os_version / security_patch props are what
    KeymasterClient.c-equivalent OS_VERSION / SPL get derived from.
    """
    avbtool = _import_avbtool()
    blob = vbmeta_path.read_bytes()
    header = avbtool.AvbVBMetaHeader(blob[:256])
    aux_off = 256 + header.authentication_data_block_size
    aux = blob[aux_off : aux_off + header.auxiliary_data_block_size]

    descriptors = avbtool.parse_descriptors(aux)
    props: dict[str, str] = {}
    for desc in descriptors:
        if isinstance(desc, avbtool.AvbPropertyDescriptor):
            key = desc.key.decode() if isinstance(desc.key, bytes) else desc.key
            value = desc.value.decode() if isinstance(desc.value, bytes) else desc.value
            props[key] = value
    return props


def _encode_os_version(version: str) -> int:
    """Bootloader-level OS version encoding from VerifiedBoot.c::ParseFooterOsVersion
    in external/edk2-uefi.lnx.5.0.r10-rel/QcomModulePkg/Library/avb/.

        ((Major << 14) | (Minor << 7) | SubMinor)

    Major: 18-bit field (max 262143), Minor / SubMinor: 7-bit each (max 127).
    For "16.0.0": (16 << 14) | 0 | 0 = 0x40000.
    """
    parts = (version or "0").split(".")
    while len(parts) < 3:
        parts.append("0")
    major, minor, sub = (int(parts[i]) for i in range(3))
    if minor > 0x7F or sub > 0x7F:
        raise SystemExit(f"OS version {version!r} components exceed 7-bit limit")
    return (major << 14) | (minor << 7) | sub


def _encode_spl(spl: str) -> int:
    """Bootloader-level SPL encoding from VerifiedBoot.c::ParseFooterSecPatch:

        ((Day << 11) | ((Year - 2000) << 4) | Month)

    Day: 5-bit (1..31), Year-since-2000: 7-bit (2000..2127), Month: 4-bit (1..12).
    For "2026-04-01": (1 << 11) | (26 << 4) | 4 = 0x9A4.

    TZ on canoe re-encodes this back to YYYYMMDD for the CSR's boot_patch_level
    field, so both directions of the round-trip have to use this exact formula
    or the CSR comes out with garbage values (e.g. 21181202 from a 202604 input).
    """
    m = re.match(r"^(\d{4})-(\d{2})-(\d{2})", spl or "")
    if not m:
        raise SystemExit(f"unrecognized security patch format: {spl!r} (expected YYYY-MM-DD)")
    year, month, day = (int(g) for g in m.groups())
    if year < 2000 or year > 2127:
        raise SystemExit(f"year {year} out of bootloader-encodable range (2000-2127)")
    if month < 1 or month > 12:
        raise SystemExit(f"month {month} out of range")
    if day < 1 or day > 31:
        raise SystemExit(f"day {day} out of range")
    return (day << 11) | ((year - 2000) << 4) | month


def _format_digest(digest: bytes) -> str:
    rows = []
    for i in range(0, len(digest), 8):
        chunk = ", ".join(f"0x{b:02x}" for b in digest[i : i + 8])
        rows.append(f"    {chunk}")
    return ", \\\n".join(rows)


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Generate tools/keymaster_overrides.generated.h from a vbmeta.img"
    )
    ap.add_argument("vbmeta", type=Path, help="vbmeta.img from a fresh OEM OTA")
    ap.add_argument(
        "-o",
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help=f"output header path (default: {DEFAULT_OUTPUT})",
    )
    ap.add_argument(
        "--os-version-prop",
        default="com.android.build.boot.os_version",
        help="vbmeta property name for the OS version",
    )
    ap.add_argument(
        "--spl-prop",
        default="com.android.build.boot.security_patch",
        help="vbmeta property name for the security patch level",
    )
    args = ap.parse_args()

    if not args.vbmeta.is_file():
        raise SystemExit(f"vbmeta image not found: {args.vbmeta}")

    pubkey = _read_pubkey_blob(args.vbmeta)
    props = _parse_props(args.vbmeta)

    rot_digest = hashlib.sha256(pubkey + b"\x00").digest()  # GREEN, IsUnlocked=0
    pubkey_digest = hashlib.sha256(pubkey).digest()  # GREEN BootState pubkey hash

    os_ver_str = props.get(args.os_version_prop, "16.0.0")
    spl_str = props.get(args.spl_prop, "2026-04-01")
    os_ver = _encode_os_version(os_ver_str)
    spl = _encode_spl(spl_str)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "/* AUTO-GENERATED by tools/ota_to_overrides.py — do not edit by hand. */\n"
        f"/* Source vbmeta: {args.vbmeta} */\n"
        f"/* SHA256 of vbmeta.img: {hashlib.sha256(args.vbmeta.read_bytes()).hexdigest()} */\n"
        f"/* OS version: {os_ver_str!r} -> 0x{os_ver:x} */\n"
        f"/* SPL:        {spl_str!r} -> {spl} (decimal) */\n"
        "#ifndef KEYMASTER_OVERRIDES_GENERATED_H\n"
        "#define KEYMASTER_OVERRIDES_GENERATED_H\n"
        "\n"
        "/* RoT digest: SHA256(AVB pubkey || IsUnlocked=0x00) */\n"
        "#define KM_OVERRIDE_ROT_DIGEST \\\n"
        f"{_format_digest(rot_digest)}\n"
        "\n"
        "/* BootState publicKey: SHA256(AVB pubkey) */\n"
        "#define KM_OVERRIDE_PUBKEY_DIGEST \\\n"
        f"{_format_digest(pubkey_digest)}\n"
        "\n"
        f"#define KM_OVERRIDE_COLOR            0u   /* GREEN */\n"
        f"#define KM_OVERRIDE_IS_UNLOCKED      0u   /* locked */\n"
        f"#define KM_OVERRIDE_SYSTEM_VERSION   0x{os_ver:x}u\n"
        f"#define KM_OVERRIDE_SYSTEM_SPL       0x{spl:X}u\n"
        "\n"
        "#endif /* KEYMASTER_OVERRIDES_GENERATED_H */\n"
    )

    print(f"Wrote {args.output}")
    print(f"  RoT digest    = {rot_digest.hex()}")
    print(f"  Pubkey digest = {pubkey_digest.hex()}")
    print(f"  OS version    = {os_ver_str!r} -> 0x{os_ver:x}")
    print(f"  SPL           = {spl_str!r} -> 0x{spl:X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
