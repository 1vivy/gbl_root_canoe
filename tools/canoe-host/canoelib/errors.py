"""The one fatal-error type the host tools raise."""

from __future__ import annotations


class CanoeError(Exception):
    """A fatal, operator-facing failure.

    Every entry point turns this into `<prog>: error: <message>` on stderr and
    exit status 1. That is the contract the shell drivers had, and the packaged
    flashers, the READMEs and the tests all read it.
    """

    def __init__(self, message: str) -> None:
        super().__init__(message)
        self.message: str = message
