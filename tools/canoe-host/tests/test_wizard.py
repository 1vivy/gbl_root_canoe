"""Tests for the host wizard's unified install questionnaire."""

from __future__ import annotations

from pathlib import Path

import pytest

from canoelib import wizard
from canoelib.build import Derived
from canoelib.layout import Toolkit


@pytest.mark.parametrize(
    ("mode", "yes_answers", "expected_yes"),
    [
        pytest.param(
            "0",
            [True],
            ["Generate a boot entry from these matching stock files?"],
            id="mode-zero",
        ),
        pytest.param(
            "1",
            [True, False, True],
            [
                (
                    "A custom recovery must be grafted with the vbmeta tool, "
                    "flashed, and returned here. Proceed?"
                ),
                "Patch vendor_boot to blacklist oplus_secure_guard_new?",
                "Generate a boot entry from these matching stock files?",
            ],
            id="mode-one",
        ),
    ],
)
def test_interactive_question_order_and_mode_gate(
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
    mode: str,
    yes_answers: list[bool],
    expected_yes: list[str],
) -> None:
    """Given a mode, ask only the decisions that mode can use, in order."""
    images = tmp_path / "images"
    images.mkdir()
    (images / "abl.img").write_bytes(b"ABL")
    (images / "vbmeta.img").write_bytes(b"VBMETA")
    monkeypatch.setattr(
        Toolkit, "shipped", classmethod(lambda cls: Toolkit(tmp_path))
    )
    monkeypatch.setattr(
        wizard.build,
        "derive",
        lambda toolkit: Derived(gbl_patched=True),
    )
    monkeypatch.setattr(wizard.stage, "install", lambda argv: None)

    choices: list[str] = []
    yes_questions: list[str] = []
    answer_iter = iter(yes_answers)
    monkeypatch.setattr(
        wizard,
        "ask_choice",
        lambda question, choices_arg, default: (
            choices.append(question), "a" if question.startswith("Which slot") else mode
        )[1],
    )

    def answer(question: str, *, default: bool) -> bool:
        yes_questions.append(question)
        return next(answer_iter)
    monkeypatch.setattr(wizard, "ask_yes_no", answer)
    wizard._interactive()

    assert choices == ["Which slot is currently active", "Which mode"]
    assert yes_questions == expected_yes


def test_help_needs_no_toolkit(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """Given a help flag, answer without constructing a toolkit or starting a flow."""
    monkeypatch.setattr(
        wizard, "_interactive", lambda: (_ for _ in ()).throw(AssertionError())
    )
    assert wizard.entry(["--help"]) == 0
    printed = capsys.readouterr().out
    for command in wizard._COMMANDS:
        assert command in printed


def test_unknown_command_reports_usage_and_fails(capsys: pytest.CaptureFixture[str]) -> None:
    """Given an unknown command, identify it and show the supported grammar."""
    assert wizard.entry(["instal"]) != 0
    captured = capsys.readouterr()
    combined = captured.out + captured.err
    assert "instal" in combined
    assert "install" in combined
