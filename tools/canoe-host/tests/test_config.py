"""Contract tests for the host canoe.cfg parser and verifier."""

from __future__ import annotations

from pathlib import Path

import pytest

from canoelib.config import (
    Config,
    ConfigEntry,
    ConfigError,
    read_config,
    serialize_config,
    verify_config,
)


def test_verifier_round_trips_a_canonical_config(tmp_path: Path) -> None:
    """Given canonical bytes from the shared format, verification preserves values."""
    path = tmp_path / "canoe.cfg"
    original = Config(
        (ConfigEntry("android-a", "Android (slot A)", "boot.efi", 1, "active"),),
        generation=4,
        timeout=5,
        default="android-a",
        mode=1,
        devinfo_repair="asneeded",
    )
    path.write_text(serialize_config(original, generation=4), encoding="ascii")

    parsed = verify_config(path)

    assert parsed == original


def test_verifier_rejects_noncanonical_bytes(tmp_path: Path) -> None:
    """Given a parseable config with an extra byte, verification refuses it."""
    path = tmp_path / "canoe.cfg"
    config = Config(
        (
            ConfigEntry("android-a", "Android (slot A)", "boot.efi", 1, "active"),
            ConfigEntry("android-b", "Android (slot B)", "boot_b.efi", 2, "inactive"),
            ConfigEntry("android-backup", "Android (previous)", "boot_backup.efi", 0, "backup"),
        ),
        default="android-a",
        mode=1,
    )
    path.write_text(serialize_config(config) + "\n", encoding="ascii")

    with pytest.raises(ConfigError, match="byte-identical"):
        verify_config(path)

    parsed = read_config(path)
    assert parsed.entries == config.entries
    assert [entry.role for entry in parsed.entries] == ["active", "inactive", "backup"]
    assert [entry.mode for entry in parsed.entries] == [1, 2, 0]
