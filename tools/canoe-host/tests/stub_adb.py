#!/usr/bin/env python3
"""Stub adb for the canoe host-tool fixtures. Not shipped in any toolkit.

Models a fake device rooted at $STUB_DEV:

    /persist                  -> $STUB_DEV/persist
    /mnt/vendor/persist       -> $STUB_DEV/vendor_persist
    /tmp                      -> $STUB_DEV/tmp
    /proc/mounts              -> $STUB_DEV/proc_mounts
    /proc/cmdline             -> $STUB_DEV/cmdline
    /dev/block/by-name/abl_a  -> $STUB_DEV/abl_a.bin
    ... likewise abl_b, vbmeta_a, vbmeta_b

Device commands run through a real `sh`, so the device-side transaction script
is exercised as written rather than simulated.

Fault injection:
    $STUB_STATE    transport reported by get-state (default: recovery)
    $STUB_FAIL     substring; any shell/push/pull command containing it fails
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

DEV = Path(os.environ["STUB_DEV"])
FAIL = os.environ.get("STUB_FAIL", "")
LOSE_EXIT = os.environ.get("STUB_LOSE_EXIT") == "1"
LOG = DEV / "adb.log"
# Models an adbd without shell protocol v2: `adb shell` exits 0 whatever the
# remote command did. Real recoveries in the wild still do this, and it is why
# the driver cannot treat a zero exit as proof that the transaction committed.

# /tmp/ MUST be rewritten first: every later rule injects $STUB_DEV paths, and
# re-scanning those for "/tmp/" would rewrite them a second time.
REMAPS = (
    ("/tmp/", f"{DEV / 'tmp'}/"),
    ("/dev/block/by-name/abl_a", str(DEV / "abl_a.bin")),
    ("/dev/block/by-name/abl_b", str(DEV / "abl_b.bin")),
    ("/dev/block/by-name/vbmeta_a", str(DEV / "vbmeta_a.bin")),
    ("/dev/block/by-name/vbmeta_b", str(DEV / "vbmeta_b.bin")),
    # Unslotted names, for a non-A/B fixture. They MUST come after the slotted
    # ones: those are already rewritten by the time these run, so `abl_a` can
    # never be mangled into `<dev>/abl.bin_a`.
    ("/dev/block/by-name/abl", str(DEV / "abl.bin")),
    ("/dev/block/by-name/vbmeta", str(DEV / "vbmeta.bin")),
    ("/proc/cmdline", str(DEV / "cmdline")),
    ("/proc/mounts", str(DEV / "proc_mounts")),
    ("/mnt/vendor/persist", str(DEV / "vendor_persist")),
    ("/persist", str(DEV / "persist")),
)


def remap(text: str) -> str:
    for src, dst in REMAPS:
        text = text.replace(src, dst)
    return text


def log(kind: str, text: str) -> None:
    with LOG.open("a", encoding="utf-8") as handle:
        handle.write(f"{kind}: {text}\n")


def main(argv: list[str]) -> int:
    while argv[:1] == ["-s"]:
        argv = argv[2:]
    if not argv:
        return 1
    op, rest = argv[0], argv[1:]

    # The fake device reports `recovery`, which is what a TWRP-derived custom
    # recovery reports and the environment these tools are documented for.
    if op == "get-state":
        sys.stdout.write(os.environ.get("STUB_STATE", "recovery") + "\n")
        return 0

    # Regression guard. `adb wait-for-device` waits for state=device
    # specifically and therefore hangs forever against a recovery, so no canoe
    # tool may use it. Failing here turns a reintroduction into a test failure
    # instead of a hang.
    if op == "wait-for-device":
        log("FAULT", "wait-for-device is unusable in recovery; poll get-state")
        sys.stderr.write("stub_adb: wait-for-device is forbidden in canoe tools\n")
        return 1

    if op == "shell":
        return shell(" ".join(rest))
    if op in ("push", "pull"):
        return transfer(op, rest[0], rest[1])
    return 0



def shell(command: str) -> int:
    log("shell", command)
    if FAIL and FAIL in command:
        log("FAULT", command)
        return 1
    line = remap(command)
    # Host stand-ins for the Android-only utilities the tools call.
    line = line.replace("blockdev --getsize64", "wc -c <")
    line = line.replace("blockdev --setrw", "true ")
    line = line.replace("getprop ro.boot.slot_suffix", f"cat {DEV / 'slot_suffix'}")
    done = subprocess.run(["sh", "-c", line], capture_output=True, text=True, check=False)
    sys.stdout.write(done.stdout)
    sys.stderr.write(done.stderr)
    return 0 if LOSE_EXIT else done.returncode


def transfer(op: str, src: str, dst: str) -> int:
    log(op, f"{src} -> {dst}")
    if FAIL and (FAIL in src or FAIL in dst):
        log("FAULT", f"{op} {src} -> {dst}")
        return 1
    real_src = Path(src if op == "push" else remap(src))
    real_dst = Path(remap(dst) if op == "push" else dst)
    real_dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(real_src, real_dst)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
