"""Tests for the wizard's mode-specific question flow."""

from __future__ import annotations

from pathlib import Path

from canoelib import wizard
from canoelib.build import Derived
from canoelib.layout import Toolkit


def test_vendor_boot_question_exists_only_for_mode_one(monkeypatch, tmp_path: Path) -> None:
    """Given each mode, only Mode 1 adds the recovery and vendor_boot decisions."""
    images = tmp_path / "images"
    images.mkdir()
    (images / "abl.img").write_bytes(b"ABL")
    (images / "vbmeta.img").write_bytes(b"VBMETA")
    monkeypatch.setattr(Toolkit, "shipped", classmethod(lambda cls: Toolkit(tmp_path)))
    monkeypatch.setattr(wizard, "_wait_for_images", lambda toolkit: None)
    monkeypatch.setattr(wizard.build, "derive", lambda toolkit: Derived(True))
    monkeypatch.setattr(wizard.stage, "entry", lambda argv: 0)

    calls: list[bool] = []

    def answers(question: str, default: bool) -> bool:
        calls.append(default)
        return True

    monkeypatch.setattr(wizard, "ask_yes_no", answers)
    monkeypatch.setattr(wizard, "ask_choice", lambda question, choices, default: "0")
    wizard._interactive()
    mode_zero_calls = len(calls)

    calls.clear()
    monkeypatch.setattr(wizard, "ask_choice", lambda question, choices, default: "1")
    monkeypatch.setattr(wizard, "ask_yes_no", answers)
    wizard._interactive()

    assert mode_zero_calls == 1
    assert len(calls) == 3


def test_help_needs_no_toolkit_and_lists_every_subcommand(monkeypatch, capsys) -> None:
    """`--help` is the most-typed flag on the new single entry point.

    It must answer before anything else runs: it cannot require a toolkit
    directory to exist, and on Windows it must not trigger the one-time ext4
    tool download. Both would make asking what the tool does do real work.
    """
    def explode() -> None:
        raise AssertionError("--help must not reach the Windows tool fetch")

    monkeypatch.setattr(wizard, "_ensure_windows_tools", explode)
    monkeypatch.setattr(wizard, "_interactive", explode)

    for flag in ("-h", "--help", "help"):
        capsys.readouterr()
        assert wizard.entry([flag]) == 0
        printed = capsys.readouterr().out
        for command in wizard._COMMANDS:
            assert command in printed, f"{flag} omitted {command}"


def test_unknown_command_reports_usage_and_fails(monkeypatch, capsys) -> None:
    """A typo must name itself and show the grammar, not just refuse."""
    monkeypatch.setattr(wizard, "_ensure_windows_tools", lambda: None)
    assert wizard.entry(["instal"]) != 0
    captured = capsys.readouterr()
    combined = captured.out + captured.err
    assert "instal" in combined
    assert "install" in combined
