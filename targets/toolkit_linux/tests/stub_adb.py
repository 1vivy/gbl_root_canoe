#!/usr/bin/env python3
"""Stub adb for the canoe script fixtures. Not shipped in any toolkit.

Models a fake device rooted at $STUB_DEV:

    /persist                  -> $STUB_DEV/persist
    /tmp                      -> $STUB_DEV/tmp
    /proc/mounts              -> $STUB_DEV/proc_mounts
    /proc/cmdline             -> $STUB_DEV/cmdline
    /dev/block/by-name/efisp  -> $STUB_DEV/efisp.bin
    /dev/block/by-name/abl_a  -> $STUB_DEV/abl_a.bin
    ... likewise vbmeta_a

Fault injection:
    $STUB_FAIL     substring; any shell/push/pull command containing it fails
    $STUB_CORRUPT  '1' makes the efisp write land wrong bytes, so the
                   byte-for-byte readback comparison must catch it
"""
import os
import shutil
import subprocess
import sys

DEV = os.environ["STUB_DEV"]
FAIL = os.environ.get("STUB_FAIL", "")
CORRUPT = os.environ.get("STUB_CORRUPT") == "1"
LOG = os.path.join(DEV, "adb.log")

argv = sys.argv[1:]
while argv and argv[0] == "-s":
    argv = argv[2:]


def remap(path: str) -> str:
    # /tmp/ MUST be rewritten first: every later rule injects $STUB_DEV paths,
    # and re-scanning those for "/tmp/" would rewrite them a second time.
    path = path.replace("/tmp/", os.path.join(DEV, "tmp") + "/")
    for src, dst in (
        ("/dev/block/by-name/efisp", os.path.join(DEV, "efisp.bin")),
        ("/dev/block/by-name/abl_a", os.path.join(DEV, "abl_a.bin")),
        ("/dev/block/by-name/abl_b", os.path.join(DEV, "abl_b.bin")),
        ("/dev/block/by-name/vbmeta_a", os.path.join(DEV, "vbmeta_a.bin")),
        ("/dev/block/by-name/vbmeta_b", os.path.join(DEV, "vbmeta_b.bin")),
        ("/proc/cmdline", os.path.join(DEV, "cmdline")),
        ("/proc/mounts", os.path.join(DEV, "proc_mounts")),
        ("/mnt/vendor/persist", os.path.join(DEV, "vendor_persist")),
        ("/persist", os.path.join(DEV, "persist")),
    ):
        path = path.replace(src, dst)
    return path


def log(kind: str, text: str) -> None:
    with open(LOG, "a") as handle:
        handle.write(f"{kind}: {text}\n")


if not argv:
    sys.exit(1)

op = argv[0]

if op == "wait-for-device":
    sys.exit(0)

if op == "shell":
    cmd = " ".join(argv[1:])
    log("shell", cmd)
    if FAIL and FAIL in cmd:
        log("FAULT", cmd)
        sys.exit(1)
    line = remap(cmd)
    # Host stand-ins for the two Android-only utilities the scripts call.
    line = line.replace("blockdev --getsize64", "wc -c <")
    line = line.replace("blockdev --setrw", "true ")
    line = line.replace("getprop ro.boot.slot_suffix", "printf _a")
    if CORRUPT and "of=" + os.path.join(DEV, "efisp.bin") in line and "canoe-w.img" in line:
        line = (f"dd if=/dev/zero of={os.path.join(DEV, 'efisp.bin')} "
                "bs=4096 count=8 conv=notrunc")
    result = subprocess.run(["sh", "-c", line], capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    sys.exit(result.returncode)

if op in ("push", "pull"):
    src, dst = argv[1], argv[2]
    log(op, f"{src} -> {dst}")
    if FAIL and (FAIL in src or FAIL in dst):
        log("FAULT", f"{op} {src} -> {dst}")
        sys.exit(1)
    real_src = src if op == "push" else remap(src)
    real_dst = remap(dst) if op == "push" else dst
    os.makedirs(os.path.dirname(real_dst) or ".", exist_ok=True)
    shutil.copyfile(real_src, real_dst)
    sys.exit(0)

sys.exit(0)
