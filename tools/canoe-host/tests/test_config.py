"""Contract tests for the host canoe.cfg reader and writer."""

from __future__ import annotations

from pathlib import Path

from canoelib.config import Config, ConfigEntry, read_config, write_config


def test_writer_round_trips_a_config_and_bumps_generation(tmp_path: Path) -> None:
    """Given one generated config, reading the file preserves values and increments generation."""
    path = tmp_path / "canoe.cfg"
    original = Config(
        (ConfigEntry("android-a", "Android (slot A)", "boot.efi", 1, "active"),),
        generation=4,
        timeout=5,
        default="android-a",
        mode=1,
        devinfo_repair="asneeded",
    )

    generation = write_config(path, original)
    parsed = read_config(path)

    assert generation == 5
    assert parsed == Config(original.entries, 5, 5, "android-a", 1, "asneeded")


def test_writer_emits_active_inactive_and_backup_entries(tmp_path: Path) -> None:
    """Given three slot roles, the generated file keeps each image policy distinct."""
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

    write_config(path, config)
    parsed = read_config(path)

    assert parsed.entries == config.entries
    assert [entry.role for entry in parsed.entries] == ["active", "inactive", "backup"]
    assert [entry.mode for entry in parsed.entries] == [1, 2, 0]
