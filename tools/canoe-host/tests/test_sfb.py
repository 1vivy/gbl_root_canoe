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


def _responder(monkeypatch: pytest.MonkeyPatch, answers: dict[str, list[str]]) -> list[str]:
    """Answer getvar by variable name, so tests do not pin the call order.

    Each name consumes one queued stderr per call and repeats its last entry,
    which is what lets a retry be expressed as a two-element queue.
    """
    asked: list[str] = []

    def fastboot_run(command: Sequence[str | Path], **_: object) -> SimpleNamespace:
        name = str(list(command)[-1])
        asked.append(name)
        queue = answers.get(name, [""])
        stderr = queue.pop(0) if len(queue) > 1 else queue[0]
        return SimpleNamespace(returncode=0, stdout="", stderr=stderr)

    monkeypatch.setattr(sfb.subprocess, "run", fastboot_run)
    monkeypatch.setattr(sfb.time, "sleep", lambda _: None)
    return asked


def test_identify_reads_bds_version_and_active_slot(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given fastboot getvar output, return the two recognized identity values."""
    toolkit = _fastboot_toolkit(tmp_path)
    _responder(
        monkeypatch,
        {"canoe-bds": ["canoe-bds: 7.0.0-b1\n"], "current-slot": ["current-slot: b\n"]},
    )

    assert sfb.identify(toolkit) == sfb.Identity("7.0.0-b1", "b")


def test_identify_retries_a_single_missed_answer(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given one dropped reply, retry rather than report the device as not a BDS."""
    toolkit = _fastboot_toolkit(tmp_path)
    asked = _responder(
        monkeypatch,
        {
            "canoe-bds": ["< waiting for any device >\n", "canoe-bds: 7.0.0-b1\n"],
            "current-slot": ["current-slot: a\n"],
        },
    )

    assert sfb.identify(toolkit) == sfb.Identity("7.0.0-b1", "a")
    assert asked.count("canoe-bds") == 2


def test_identify_returns_empty_fields_for_unknown_variables(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given missing getvars, return no identity values after one retry each."""
    toolkit = _fastboot_toolkit(tmp_path)
    failed = "FAILED (remote: 'GetVar Variable Not found')\n"
    asked = _responder(monkeypatch, {"canoe-bds": [failed], "current-slot": [failed]})

    assert sfb.identify(toolkit) == sfb.Identity(None, None)
    assert asked.count("canoe-bds") == 2
    assert asked.count("current-slot") == 2


def test_identify_returns_empty_fields_without_a_device(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given fastboot's no-device failure, return no identity values."""
    toolkit = _fastboot_toolkit(tmp_path)

    def no_device(*_: Sequence[str | Path], **__: object) -> SimpleNamespace:
        raise OSError("no device attached")

    monkeypatch.setattr(sfb.subprocess, "run", no_device)

    assert sfb.identify(toolkit) == sfb.Identity(None, None)


def test_identify_rejects_an_unrecognized_slot(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given a slot value other than a or b, leave only the version identified."""
    toolkit = _fastboot_toolkit(tmp_path)
    _responder(
        monkeypatch,
        {"canoe-bds": ["canoe-bds: 7.0.0-b1\n"], "current-slot": ["current-slot: c\n"]},
    )

    assert sfb.identify(toolkit) == sfb.Identity("7.0.0-b1", None)
