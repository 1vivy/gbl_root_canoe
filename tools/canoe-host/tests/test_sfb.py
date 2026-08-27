"""Tests for the Super Fastboot identity probe."""

from __future__ import annotations

from collections.abc import Sequence
from pathlib import Path
from types import SimpleNamespace
from typing import TYPE_CHECKING

from canoelib import sfb

if TYPE_CHECKING:
    import pytest


def _fastboot_toolkit(tmp_path: Path) -> Path:
    toolkit = tmp_path / "toolkit"
    platform_tools = toolkit / "Platform-Tools"
    platform_tools.mkdir(parents=True)
    (platform_tools / "fastboot").write_bytes(b"fastboot")
    return toolkit


def test_identify_reads_bds_version_and_active_slot(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given fastboot getvar output, return the two recognized identity values."""
    toolkit = _fastboot_toolkit(tmp_path)
    responses = iter(
        (
            SimpleNamespace(returncode=0, stdout="", stderr="canoe-bds: 7.0.0-b1\n"),
            SimpleNamespace(returncode=0, stdout="", stderr="current-slot: b\n"),
        )
    )

    def fastboot_run(command: Sequence[str | Path], **_: bool) -> SimpleNamespace:
        assert list(command)[1:] == ["getvar", "canoe-bds"] or list(command)[1:] == [
            "getvar",
            "current-slot",
        ]
        return next(responses)

    monkeypatch.setattr(sfb.subprocess, "run", fastboot_run)

    assert sfb.identify(toolkit) == sfb.Identity("7.0.0-b1", "b")


def test_identify_returns_empty_fields_for_unknown_variables(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given missing getvars, return no identity values."""
    toolkit = _fastboot_toolkit(tmp_path)
    responses = iter(
        (
            SimpleNamespace(
                returncode=0,
                stdout="",
                stderr="FAILED (remote: 'GetVar Variable Not found')\n",
            ),
            SimpleNamespace(
                returncode=0,
                stdout="",
                stderr="FAILED (remote: 'GetVar Variable Not found')\n",
            ),
        )
    )
    monkeypatch.setattr(sfb.subprocess, "run", lambda *_, **__: next(responses))

    assert sfb.identify(toolkit) == sfb.Identity(None, None)


def test_identify_returns_empty_fields_without_a_device(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given fastboot's no-device failure, return no identity values."""
    toolkit = _fastboot_toolkit(tmp_path)

    def no_device(*_: Sequence[str | Path], **__: bool) -> SimpleNamespace:
        raise OSError("no device attached")

    monkeypatch.setattr(sfb.subprocess, "run", no_device)

    assert sfb.identify(toolkit) == sfb.Identity(None, None)


def test_identify_rejects_an_unrecognized_slot(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given a slot value other than a or b, leave only the version identified."""
    toolkit = _fastboot_toolkit(tmp_path)
    responses = iter(
        (
            SimpleNamespace(returncode=0, stdout="", stderr="canoe-bds: 7.0.0-b1\n"),
            SimpleNamespace(returncode=0, stdout="", stderr="current-slot: c\n"),
        )
    )
    monkeypatch.setattr(sfb.subprocess, "run", lambda *_, **__: next(responses))

    assert sfb.identify(toolkit) == sfb.Identity("7.0.0-b1", None)
