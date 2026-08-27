"""Behavioral tests for the host install path."""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from tests.conftest import FakeToolkit, ToolkitFactory


def _prepare(toolkit: FakeToolkit) -> None:
    toolkit.plant_triplet()


def _install_args(toolkit: FakeToolkit, mode: str = "2") -> tuple[str, ...]:
    return (
        "canoe",
        "install",
        "--boot-root",
        str(toolkit.device.persist),
        "--slot",
        "b",
        "--mode",
        mode,
    )

def test_install_writes_config_to_the_explicit_boot_root(make_toolkit: ToolkitFactory) -> None:
    """Given a prepared triplet, install writes only the mounted boot root."""
    toolkit = make_toolkit(live=False)
    _prepare(toolkit)
    result = toolkit.run(*_install_args(toolkit))

    assert result.returncode == 0, result.stderr
    config = (toolkit.device.boot_root / "canoe.cfg").read_text(encoding="ascii")
    assert "version 1" in config
    assert "mode 2" in config
    assert "role active" in config
    assert not (toolkit.root / "efisp" / "canoe.cfg").exists()

def test_install_mode_zero_reaches_installed_config(make_toolkit: ToolkitFactory) -> None:
    """Given mode zero, the writer stores mode zero in the active entry."""
    toolkit = make_toolkit(live=False)
    _prepare(toolkit)
    result = toolkit.run(*_install_args(toolkit, "0"))

    assert result.returncode == 0, result.stderr
    config = (toolkit.device.boot_root / "canoe.cfg").read_text(encoding="ascii")
    assert "  mode 0\n" in config


def test_install_rejects_unknown_mode_before_opening_boot_root(toolkit: FakeToolkit) -> None:
    """Given an unsupported mode, install rejects it before touching the mount."""
    _prepare(toolkit)
    result = toolkit.run(*_install_args(toolkit, "9"))

    assert result.returncode != 0
    assert "must be 0, 1 or 2" in result.stderr
    assert not toolkit.device.boot_root.joinpath("canoe.cfg").exists()


def test_install_requires_an_explicit_slot(toolkit: FakeToolkit) -> None:
    """Given no slot, install refuses because BDS exports no current-slot value."""
    _prepare(toolkit)
    result = toolkit.run(
        "canoe",
        "install",
        "--boot-root",
        str(toolkit.device.persist),
        "--mode",
        "0",
    )

    assert result.returncode != 0
    assert "required" in result.stderr
