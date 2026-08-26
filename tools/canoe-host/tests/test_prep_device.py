"""End-to-end tests for standalone device preparation."""

from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from tests.conftest import FakeToolkit


def _supplied_pair(toolkit: FakeToolkit) -> tuple[Path, Path]:
    abl = toolkit.root / "supplied-abl.img"
    vbmeta = toolkit.root / "supplied-vbmeta.img"
    abl.write_bytes(b"SUPPLIED-ABL")
    vbmeta.write_bytes(b"SUPPLIED-VBMETA")
    return abl, vbmeta


def test_prep_device_requires_the_matched_pair(toolkit: FakeToolkit) -> None:
    """Given only --abl, the launcher rejects the pair before contacting adb."""
    abl, _ = _supplied_pair(toolkit)
    result = toolkit.run("canoe", "prep-device", "--abl", str(abl))
    assert result.returncode == 1
    assert "must be given together" in result.stderr
    assert toolkit.device.log == ""


def test_prep_device_supplied_pair_never_contacts_device(toolkit: FakeToolkit) -> None:
    """Given both supplied images, derivation uses them without an adb transport."""
    abl, vbmeta = _supplied_pair(toolkit)
    result = toolkit.run(
        "canoe",
        "prep-device",
        "--abl",
        str(abl),
        "--vbmeta",
        str(vbmeta),
        "--keep-images",
    )
    assert result.returncode == 0, result.stderr
    assert toolkit.device.log == ""
    assert toolkit.read("images/abl.img") == abl.read_bytes()
    assert toolkit.read("images/vbmeta.img") == vbmeta.read_bytes()


def test_prep_device_inactive_uses_the_non_active_slot(toolkit: FakeToolkit) -> None:
    """Given distinct A/B payloads, inactive derives boot.efi from the other slot."""
    toolkit.device.set_partition("abl_b", b"INACTIVE-ABL-PAYLOAD")
    toolkit.device.set_partition("vbmeta_b", b"INACTIVE-VBMETA-PAYLOAD")
    result = toolkit.run("canoe", "prep-device", "--slot", "inactive", "--keep-images")
    assert result.returncode == 0, result.stderr
    assert "the slot an adb sideload has just written" in result.stdout
    assert b"INACTIVE-ABL-PAYLOAD" in toolkit.read("efisp/boot.efi")
    assert toolkit.read("images/abl.img") == b"INACTIVE-ABL-PAYLOAD"


def test_prep_device_handles_non_ab_partitions(toolkit: FakeToolkit) -> None:
    """Given no slot suffix, the bare non-A/B partitions are used."""
    toolkit.device.set_slot("")
    toolkit.device.set_partition("abl", b"NON-AB-ABL-PAYLOAD")
    toolkit.device.set_partition("vbmeta", b"NON-AB-VBMETA-PAYLOAD")
    result = toolkit.run("canoe", "prep-device", "--keep-images")
    assert result.returncode == 0, result.stderr
    assert "no slot suffix reported; assuming a non-A/B layout" in result.stdout
    assert b"NON-AB-ABL-PAYLOAD" in toolkit.read("efisp/boot.efi")
    assert toolkit.read("images/abl.img") == b"NON-AB-ABL-PAYLOAD"
    assert toolkit.read("images/vbmeta.img") == b"NON-AB-VBMETA-PAYLOAD"


def test_prep_device_rejects_unknown_slot(toolkit: FakeToolkit) -> None:
    """Given an unknown slot token, parsing fails with the accepted values."""
    result = toolkit.run("canoe", "prep-device", "--slot", "nonsense")
    assert result.returncode == 1
    assert "--slot must be _a, _b, active or inactive" in result.stderr


def test_prep_device_removes_pulled_images_by_default(toolkit: FakeToolkit) -> None:
    """Given a device source and no keep flag, temporary pulled images are removed."""
    result = toolkit.run("canoe", "prep-device")
    assert result.returncode == 0, result.stderr
    assert not (toolkit.root / "images" / "abl.img").exists()
    assert not (toolkit.root / "images" / "vbmeta.img").exists()


def test_prep_device_keep_images_preserves_pulled_pair(toolkit: FakeToolkit) -> None:
    """Given --keep-images, the source pair remains available after derivation."""
    result = toolkit.run("canoe", "prep-device", "--keep-images")
    assert result.returncode == 0, result.stderr
    assert (toolkit.root / "images" / "abl.img").is_file()
    assert (toolkit.root / "images" / "vbmeta.img").is_file()


def test_prep_device_missing_gbl_reports_fastboot_step(toolkit: FakeToolkit) -> None:
    """Given an ABL without GBL, the report directs the operator to flash one."""
    result = toolkit.run("canoe", "prep-device", STUB_NO_GBL="1")
    assert result.returncode == 0, result.stderr
    assert "does NOT carry the GBL vulnerability" in result.stdout
    assert "fastboot flash abl <vulnerable>.img" in result.stdout
