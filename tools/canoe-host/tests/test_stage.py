"""Behavioral tests for canoe install and the temporary one-shot path."""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from tests.conftest import FakeToolkit


def _prepare(toolkit: FakeToolkit) -> None:
    toolkit.plant_triplet()


def test_install_writes_config_and_bds_without_tail_records(toolkit: FakeToolkit) -> None:
    """Given a prepared triplet, install writes canoe.cfg and the raw BDS image."""
    _prepare(toolkit)
    result = toolkit.run("canoe", "install", "--mode", "2")
    before_config = toolkit.root / "efisp" / "canoe.cfg"
    assert result.returncode == 0, result.stderr
    config = (toolkit.device.boot_root / "canoe.cfg").read_text(encoding="ascii")
    assert "version 1" in config
    assert "mode 2" in before_config.read_text(encoding="ascii")
    assert "role active" in config
    assert toolkit.device.efisp.read_bytes().startswith(b"MZ-NEW-BDS-IMAGE")


def test_install_skip_bds_changes_only_the_boot_root(toolkit: FakeToolkit) -> None:
    """Given --skip-bds, the config tree commits while the raw efisp bytes stay intact."""
    _prepare(toolkit)
    before = toolkit.device.efisp.read_bytes()
    result = toolkit.run("canoe", "install", "--skip-bds", "--mode", "0")

    assert result.returncode == 0, result.stderr
    assert toolkit.device.efisp.read_bytes() == before
    assert (toolkit.device.boot_root / "canoe.cfg").is_file()


def test_install_rejects_unknown_mode_before_adb(toolkit: FakeToolkit) -> None:
    """Given an unsupported mode, install rejects it without contacting the device."""
    _prepare(toolkit)
    result = toolkit.run("canoe", "install", "--mode", "9")

    assert result.returncode != 0
    assert "must be 0, 1 or 2" in result.stderr
    assert toolkit.device.log == ""


def test_oneshot_writes_only_explicit_host_output(toolkit: FakeToolkit) -> None:
    """Given a vulnerable ABL, oneshot leaves boot root and efisp untouched."""
    abl = toolkit.root / "images" / "vulnerable-abl.img"
    abl.write_bytes(b"KNOWN-STOCK-ABL")
    before_efisp = toolkit.device.efisp.read_bytes()
    before_boot = toolkit.device.boot_root.joinpath("boot.efi").read_bytes()
    output = toolkit.root / "oneshot.efi"
    result = toolkit.run("canoe", "oneshot", "--abl", str(abl), "--mode", "1", "--out", str(output))

    assert result.returncode == 0, result.stderr
    assert output.read_bytes().startswith(b"PATCHED-")
    assert toolkit.device.efisp.read_bytes() == before_efisp
    assert toolkit.device.boot_root.joinpath("boot.efi").read_bytes() == before_boot
