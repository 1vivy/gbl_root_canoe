"""The anti-drift gate for the two-implementation design.

The host runs `canoelib/boottree.py` and the device runs
`tools/canoe-device/canoe_device_install.sh`. They share no code -- the host
must not invoke `sh`, because the Windows package ships no shell -- so nothing
but a test keeps them from diverging. These cases run both against identical
fixtures and require the boot roots to come out the same.

`canoe.cfg` is compared byte for byte on purpose. Generation is part of those
bytes, and the shell writer bumps it once per invocation, so an implementation
that rendered the desired end state in a single write instead of replaying the
same operation sequence would fail here rather than in the field.
"""

from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
from pathlib import Path

import pytest

from canoelib.boottree import install_tree
from canoelib.layout import GM2P_BYTES, TZMAP_BYTES

REPO = Path(__file__).resolve().parents[3]
INSTALL_SH = REPO / "tools" / "canoe-device" / "canoe_device_install.sh"
BOOT_ENTRY_SH = REPO / "tools" / "canoe-device" / "canoe_boot_entry.sh"
CUSTOM_ROW = (
    "\nentry lineage\n  title LineageOS\n  image roms/lineage.efi\n  mode 2\n  role other\n"
)
PASSTHROUGH_ROW = (
    "\nentry android-a\n  title Android (slot A)\n  image boot_a.efi\n  mode 1\n  role inactive\n"
)
BASE_CONFIG = (
    "version 1\ngeneration 7\ntimeout 5\ndefault android-b\nmode 1\ndevinfo-repair asneeded\n\n"
    "entry android-b\n  title Android (slot B)\n  image boot.efi\n  mode 1\n  role active\n"
)


def _stage(base: Path) -> Path:
    staged = base / "stage"
    (staged / "tools").mkdir(parents=True)
    _ = (staged / "boot.efi").write_bytes(b"NEW-LOADER")
    _ = (staged / "boot.efi.gm2p").write_bytes(bytes(range(GM2P_BYTES)))
    _ = (staged / "boot.efi.tzmap").write_bytes(bytes(TZMAP_BYTES))
    _ = (staged / "tools" / "BLTools.efi").write_bytes(b"TOOL")
    return staged


def _plant_live(boot_root: Path) -> None:
    _ = (boot_root / "boot.efi").write_bytes(b"OLD-LOADER")
    _ = (boot_root / "boot.efi.gm2p").write_bytes(bytes(range(GM2P_BYTES)))
    _ = (boot_root / "boot.efi.tzmap").write_bytes(bytes(TZMAP_BYTES))
    (boot_root / "tools").mkdir(exist_ok=True)
    _ = (boot_root / "tools" / "BLTools.efi").write_bytes(b"OLDTOOL")


def _plant_passthrough(boot_root: Path) -> None:
    _plant_live(boot_root)
    _ = (boot_root / "canoe.cfg").write_text(BASE_CONFIG + PASSTHROUGH_ROW, encoding="ascii")
    _ = (boot_root / "boot_a.efi").write_bytes(b"PASSTHROUGH")
    _ = (boot_root / "boot_a.efi.gm2p").write_bytes(bytes(range(GM2P_BYTES)))
    _ = (boot_root / "boot_a.efi.tzmap").write_bytes(bytes(TZMAP_BYTES))


def _plant_custom_row(boot_root: Path) -> None:
    _plant_live(boot_root)
    _ = (boot_root / "canoe.cfg").write_text(BASE_CONFIG + CUSTOM_ROW, encoding="ascii")


def _fingerprint(boot_root: Path) -> dict[str, str]:
    """Every path under the boot root, with content digests for files."""
    out: dict[str, str] = {}
    for path in sorted(boot_root.rglob("*")):
        key = path.relative_to(boot_root).as_posix()
        out[key] = "<dir>" if path.is_dir() else hashlib.sha256(path.read_bytes()).hexdigest()
    return out


def _run_shell(staged: Path, boot_root: Path, *, mode: str, slot: str) -> None:
    result = subprocess.run(
        ["sh", str(INSTALL_SH), str(staged), str(boot_root)],
        env={
            **os.environ,
            "CANOE_BOOT_ENTRY": str(BOOT_ENTRY_SH),
            "CANOE_ACTIVE_SLOT": slot,
            "CANOE_MODE": mode,
        },
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        pytest.fail(f"canoe_device_install.sh failed: {result.stderr}")


def _read(boot_root: Path, name: str) -> bytes:
    return (boot_root / name).read_bytes()


@pytest.mark.parametrize(
    "plant",
    [
        pytest.param(None, id="first-install"),
        pytest.param(_plant_live, id="update"),
        pytest.param(_plant_passthrough, id="passthrough-migration"),
        pytest.param(_plant_custom_row, id="hand-added-row"),
    ],
)
def test_both_implementations_leave_the_same_boot_root(
    tmp_path: Path, plant: object, capsys: pytest.CaptureFixture[str]
) -> None:
    """Given one fixture, when each implementation installs, then the boot roots match."""
    roots: dict[str, Path] = {}
    for name in ("python", "shell"):
        boot_root = tmp_path / name / "efisp"
        boot_root.mkdir(parents=True)
        if callable(plant):
            plant(boot_root)
        staged = _stage(tmp_path / name)
        if name == "python":
            _ = install_tree(boot_root, staged, mode=0, active_slot="_b")
        else:
            _run_shell(staged, boot_root, mode="0", slot="_b")
        roots[name] = boot_root
    _ = capsys.readouterr()

    python, shell = _fingerprint(roots["python"]), _fingerprint(roots["shell"])

    assert _read(roots["python"], "canoe.cfg") == _read(roots["shell"], "canoe.cfg")
    assert _read(roots["python"], ".canoe.gen") == _read(roots["shell"], ".canoe.gen")
    assert python == shell
    leftovers = [name for name in python if name.startswith(".canoe.") and name != ".canoe.gen"]
    assert leftovers == []


def test_the_device_script_is_where_the_parity_gate_expects_it() -> None:
    """Given the repo layout, the shell implementation this gate compares against exists."""
    assert INSTALL_SH.is_file()
    assert BOOT_ENTRY_SH.is_file()
    assert shutil.which("sh") is not None
