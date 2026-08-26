"""Running the toolkit's own binaries.

Every call goes through `run`, so there is exactly one place that decides how a
child process is spawned. Nothing is passed through a shell: the argument list
reaches the binary verbatim, which is the whole class of bug (quoting, caret
escapes, word splitting) that the batch and shell drivers kept shipping.
"""

from __future__ import annotations

import subprocess
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

from .errors import CanoeError


@dataclass(frozen=True, slots=True)
class Completed:
    """The outcome of one command."""

    code: int
    out: str
    err: str

    @property
    def ok(self) -> bool:
        """True when the command exited zero."""
        return self.code == 0


def run(
    command: Sequence[str | Path],
    *,
    cwd: Path | None = None,
    log: Path | None = None,
) -> Completed:
    """Run `command` and capture its output.

    With `log`, stderr is merged into stdout and the combined text is written
    there. That is what `cmd > log 2>&1` did, and the merge keeps the
    interleaving that makes the patch log readable.
    """
    argv = [str(part) for part in command]
    try:
        proc = subprocess.run(  # noqa: S603 - argv list, never a shell string
            argv,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT if log is not None else subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
    except OSError as exc:
        raise CanoeError(f"could not run {argv[0]}: {exc}") from exc
    out = proc.stdout or ""
    if log is not None:
        log.write_text(out, encoding="utf-8")
    return Completed(proc.returncode, out, proc.stderr or "")
