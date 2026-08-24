"""Operator-facing output, and the boundary that every entry point runs behind.

The shapes here are contracts, not decoration: `[*] ` opens a step, four
leading spaces mark a detail under it, `    WARNING: ` is a non-fatal problem
and `<prog>: error: ` on stderr is a fatal one. The wiki, the packaged flashers
and the tests all match on them.
"""

from __future__ import annotations

import sys
from collections.abc import Callable, Sequence

from .errors import CanoeError


def step(text: str) -> None:
    """Announce a phase of the run."""
    print(f"\n[*] {text}", flush=True)


def note(text: str) -> None:
    """Report a detail underneath the current step."""
    print(f"    {text}", flush=True)


def warn(text: str) -> None:
    """Report a problem the operator must know about but that is not fatal."""
    print(f"    WARNING: {text}", file=sys.stderr, flush=True)


def emit(text: str) -> None:
    """Write report text verbatim, for the closing summaries."""
    print(text, flush=True)


def run_entry(prog: str, run: Callable[[Sequence[str]], None], argv: Sequence[str]) -> int:
    """Run a tool body and map its outcome onto the documented exit contract."""
    try:
        run(argv)
    except CanoeError as exc:
        print(f"{prog}: error: {exc}", file=sys.stderr, flush=True)
        return 1
    except KeyboardInterrupt:
        print(f"{prog}: interrupted", file=sys.stderr, flush=True)
        return 130
    return 0
