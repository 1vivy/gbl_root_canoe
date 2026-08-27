"""Contract tests for the host boot-root install transaction."""

from __future__ import annotations

from pathlib import Path

import pytest

from canoelib.boottree import install_tree
from canoelib.config import read_config
from canoelib.errors import CanoeError
from canoelib.layout import GM2P_BYTES, TZMAP_BYTES

CUSTOM_ROW = (
    "\nentry lineage\n  title LineageOS\n  image roms/lineage.efi\n  mode 2\n  role other\n"
)

PASSTHROUGH_ROW = (
    "\nentry android-b\n  title Android (slot B)\n  image boot_b.efi\n  mode 1\n  role inactive\n"
)

def _profile(signer: int = 0x11) -> bytes:
    """A 120-byte Mode 2 profile whose public-key digest is a single repeated byte."""
    return bytes(0x38) + bytes([signer]) * 32 + bytes(GM2P_BYTES - 0x58)


def _stage(root: Path, *, signer: int = 0x11, gm2p_bytes: int = GM2P_BYTES) -> Path:
    staged = root / "stage"
    (staged / "tools").mkdir(parents=True)
    _ = (staged / "boot.efi").write_bytes(b"NEW-LOADER")
    _ = (staged / "boot.efi.gm2p").write_bytes(_profile(signer)[:gm2p_bytes])
    _ = (staged / "boot.efi.tzmap").write_bytes(bytes(TZMAP_BYTES))
    _ = (staged / "tools" / "BLTools.efi").write_bytes(b"TOOL")
    return staged


def _boot_root(root: Path) -> Path:
    boot_root = root / "efisp"
    boot_root.mkdir(parents=True)
    return boot_root


def _plant_live(boot_root: Path, *, signer: int = 0x11) -> None:
    _ = (boot_root / "boot.efi").write_bytes(b"OLD-LOADER")
    _ = (boot_root / "boot.efi.gm2p").write_bytes(_profile(signer))
    _ = (boot_root / "boot.efi.tzmap").write_bytes(bytes(TZMAP_BYTES))


def _rows(boot_root: Path) -> dict[str, str]:
    return {entry.id: entry.image for entry in read_config(boot_root / "canoe.cfg").entries}


def test_first_install_writes_one_active_row(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    """Given an empty boot root, when installing, then only the active row exists."""
    boot_root = _boot_root(tmp_path)

    receipt = install_tree(boot_root, _stage(tmp_path), mode=0, active_slot="_b")

    assert receipt.first_install is True
    assert _rows(boot_root) == {"android-b": "boot.efi"}
    config = read_config(boot_root / "canoe.cfg")
    assert config.default == "android-b"
    assert config.mode == 0
    assert (boot_root / "tools" / "BLTools.efi").read_bytes() == b"TOOL"
    assert (boot_root / ".canoe.gen").read_text(encoding="ascii").startswith("CANOEG1|-|")
    marks = capsys.readouterr().out
    assert "CANOE-MARK: first-install" in marks
    assert "CANOE-MARK: entry-set id=android-b role=active mode=0" in marks


def test_update_demotes_the_previous_generation(tmp_path: Path) -> None:
    """Given a live generation, when installing again, then it becomes the backup row."""
    boot_root = _boot_root(tmp_path)
    _plant_live(boot_root)

    receipt = install_tree(boot_root, _stage(tmp_path), mode=1, active_slot="_a")

    assert receipt.first_install is False
    assert _rows(boot_root) == {"android-a": "boot.efi", "android-backup": "boot_backup.efi"}
    assert (boot_root / "boot_backup.efi").read_bytes() == b"OLD-LOADER"
    assert (boot_root / "boot.efi").read_bytes() == b"NEW-LOADER"


def test_install_preserves_a_hand_added_row(tmp_path: Path) -> None:
    """Given a row the operator added, when installing, then it survives verbatim."""
    boot_root = _boot_root(tmp_path)
    _plant_live(boot_root)
    _ = install_tree(boot_root, _stage(tmp_path), mode=1, active_slot="_a")
    config = boot_root / "canoe.cfg"
    _ = config.write_text(config.read_text(encoding="ascii") + CUSTOM_ROW, encoding="ascii")

    _ = install_tree(boot_root, _stage(tmp_path / "again"), mode=1, active_slot="_a")

    assert _rows(boot_root)["lineage"] == "roms/lineage.efi"


def test_install_migrates_the_passthrough_row_away(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    """Given a watcher-era boot_b.efi and its row, when installing, then both are gone.

    The BDS managed-path predicate never matched that loader, so the row's mode
    and sidecars were ignored; leaving it behind is a menu entry that silently
    means something other than what it says.
    """
    boot_root = _boot_root(tmp_path)
    _plant_live(boot_root)
    _ = install_tree(boot_root, _stage(tmp_path), mode=1, active_slot="_a")
    config = boot_root / "canoe.cfg"
    _ = config.write_text(
        config.read_text(encoding="ascii")
        + CUSTOM_ROW
        + PASSTHROUGH_ROW,
        encoding="ascii",
    )
    _ = (boot_root / "boot_b.efi").write_bytes(b"PASSTHROUGH")
    _ = (boot_root / "boot_b.efi.gm2p").write_bytes(_profile())

    _ = install_tree(boot_root, _stage(tmp_path / "again"), mode=1, active_slot="_a")

    assert "android-b" not in _rows(boot_root)
    assert _rows(boot_root)["lineage"] == "roms/lineage.efi"
    assert not (boot_root / "boot_b.efi").exists()
    assert not (boot_root / "boot_b.efi.gm2p").exists()
    assert "CANOE-MARK: passthrough-row-migrated id=android-b" in capsys.readouterr().out


def test_a_wrong_sidecar_length_is_refused_before_any_write(tmp_path: Path) -> None:
    """Given a short gm2p, when installing, then nothing in the boot root changes."""
    boot_root = _boot_root(tmp_path)
    _plant_live(boot_root)
    before = sorted(path.name for path in boot_root.iterdir())

    with pytest.raises(CanoeError, match=r"boot\.efi\.gm2p must be exactly 120 bytes"):
        _ = install_tree(boot_root, _stage(tmp_path, gm2p_bytes=119), mode=1, active_slot="_a")

    assert sorted(path.name for path in boot_root.iterdir()) == before
    assert (boot_root / "boot.efi").read_bytes() == b"OLD-LOADER"


def test_a_failed_commit_restores_the_previous_generation(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    """Given the tools tree cannot be read, when installing, then the live set comes back.

    The failure lands after the triplet is already committed and the previous
    generation already demoted, which is the only rollback worth testing: an
    abort before the first write needs no undo.
    """
    boot_root = _boot_root(tmp_path)
    _plant_live(boot_root)
    _ = install_tree(boot_root, _stage(tmp_path), mode=1, active_slot="_a")
    installed = (boot_root / "canoe.cfg").read_bytes()
    staged = _stage(tmp_path / "again")
    (staged / "tools" / "BLTools.efi").chmod(0o000)

    with pytest.raises(CanoeError):
        _ = install_tree(boot_root, staged, mode=1, active_slot="_a")

    assert (boot_root / "canoe.cfg").read_bytes() == installed
    assert (boot_root / "boot.efi").read_bytes() == b"NEW-LOADER"
    assert (boot_root / "boot_backup.efi").read_bytes() == b"OLD-LOADER"
    assert (boot_root / "tools" / "BLTools.efi").read_bytes() == b"TOOL"
    assert "CANOE-MARK: pair-restored" in capsys.readouterr().out


def test_a_changed_signer_is_refused_with_the_tree_unchanged(tmp_path: Path) -> None:
    """Given a staged profile signed by another key, when installing, then it is refused."""
    boot_root = _boot_root(tmp_path)
    _plant_live(boot_root, signer=0x11)

    with pytest.raises(CanoeError, match="vbmeta signer changed"):
        _ = install_tree(boot_root, _stage(tmp_path, signer=0x22), mode=1, active_slot="_a")

    assert (boot_root / "boot.efi").read_bytes() == b"OLD-LOADER"
    assert not (boot_root / "canoe.cfg").exists()


def test_a_changed_signer_is_accepted_when_the_operator_supplied_it(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    """Given the operator supplied the vbmeta, when installing, then the change is logged.

    Supplying the file is the operator's declaration that this firmware has a
    different author, which is exactly the Custom ROM case.
    """
    boot_root = _boot_root(tmp_path)
    _plant_live(boot_root, signer=0x11)

    receipt = install_tree(
        boot_root, _stage(tmp_path, signer=0x22), mode=1, active_slot="_a", allow_new_signer=True
    )

    assert receipt.signer_changed is True
    assert (boot_root / "boot.efi").read_bytes() == b"NEW-LOADER"
    assert "CANOE-MARK: signer-changed source=supplied" in capsys.readouterr().out
