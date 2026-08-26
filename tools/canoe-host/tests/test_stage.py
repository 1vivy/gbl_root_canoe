"""Behavioral tests for canoe install and the temporary one-shot path."""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from tests.conftest import FakeToolkit, ToolkitFactory


def _prepare(toolkit: FakeToolkit) -> None:
    toolkit.plant_triplet()


def test_install_writes_config_without_raw_bds_write(toolkit: FakeToolkit) -> None:
    """Given a prepared triplet, install writes canoe.cfg but leaves efisp raw bytes alone."""
    _prepare(toolkit)
    before_efisp = toolkit.device.efisp.read_bytes()
    result = toolkit.run("canoe", "install", "--mode", "2")

    assert result.returncode == 0, result.stderr
    config = (toolkit.device.boot_root / "canoe.cfg").read_text(encoding="ascii")
    assert "version 1" in config
    assert "mode 2" in config
    assert "role active" in config
    assert toolkit.device.efisp.read_bytes() == before_efisp
    assert not (toolkit.root / "efisp" / "canoe.cfg").exists()



def test_install_mode_zero_reaches_installed_config(toolkit: FakeToolkit) -> None:
    """Given mode zero, the shared writer stores mode zero in the installed entry."""
    _prepare(toolkit)
    result = toolkit.run("canoe", "install", "--mode", "0")

    assert result.returncode == 0, result.stderr
    config = (toolkit.device.boot_root / "canoe.cfg").read_text(encoding="ascii")
    assert "  mode 0\n" in config

def test_install_rejects_unknown_mode_before_adb(toolkit: FakeToolkit) -> None:
    """Given an unsupported mode, install rejects it without contacting the device."""
    _prepare(toolkit)
    result = toolkit.run("canoe", "install", "--mode", "9")

    assert result.returncode != 0
    assert "must be 0, 1 or 2" in result.stderr
    assert toolkit.device.log == ""


def test_install_mass_storage_uses_the_same_transaction(make_toolkit: ToolkitFactory) -> None:
    """Given a mounted persist path, mass storage runs the shared transaction locally."""
    toolkit = make_toolkit()
    _prepare(toolkit)
    result = toolkit.run(
        "canoe",
        "install",
        "--boot-root",
        str(toolkit.device.persist),
        "--slot",
        "a",
        "--mode",
        "0",
    )

    assert result.returncode == 0, result.stderr
    config = (toolkit.device.boot_root / "canoe.cfg").read_text(encoding="ascii")
    assert "  mode 0\n" in config


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
