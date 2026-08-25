"""Behavioral coverage for the unified canoe_stage host driver."""

from __future__ import annotations

from typing import TYPE_CHECKING

from canoe import stage
from canoe.adb import Adb
from canoe.layout import Toolkit

if TYPE_CHECKING:
    import pytest

    from tests.conftest import FakeToolkit, ToolkitFactory


def _clean_stage(toolkit: FakeToolkit) -> None:
    """Assert the transaction cleanup contract through the fixture surface."""
    assert not toolkit.device.boot_root.joinpath(".canoe.stage").exists()


def _prepare(toolkit: FakeToolkit) -> None:
    """Install the fixture's valid derived triplet before a stage run."""
    toolkit.plant_triplet()


def _prepare_with_loader(toolkit: FakeToolkit) -> None:
    """Install a triplet and the unpatched loader used to derive its tzmap."""
    _prepare(toolkit)
    (toolkit.root / "ABL_original.efi").write_bytes(b"STAGE-ABL")



def _run_size_probe(
    toolkit: FakeToolkit,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    value: int | None,
) -> tuple[int, str]:
    """Run the real launcher body while forcing one staged-size probe result."""
    toolkit.plant_triplet()
    original = Adb.size

    def probe(adb: Adb, remote: str) -> int | None:
        if remote.endswith("boot.efi.gm2p"):
            return value
        return original(adb, remote)

    def shipped(_cls: type[Toolkit]) -> Toolkit:
        return Toolkit(toolkit.root)

    monkeypatch.chdir(toolkit.root)
    monkeypatch.setenv("STUB_DEV", str(toolkit.device.root))
    monkeypatch.setenv("CANOE_TRACE", str(toolkit.trace_path))
    monkeypatch.setenv("CANOE_ADB_WAIT", "1")
    monkeypatch.setattr(Toolkit, "shipped", classmethod(shipped))
    monkeypatch.setattr(Adb, "size", probe)
    result = stage.entry(())
    output = capsys.readouterr()
    return result, output.err


def test_green_install_commits_tree_bds_and_backup(toolkit: FakeToolkit) -> None:
    """Given a live device, installing stages and commits every artifact."""
    _prepare(toolkit)
    old_boot = toolkit.device.boot_root.joinpath("boot.efi").read_bytes()
    old_efisp = toolkit.device.efisp.read_bytes()
    result = toolkit.run("canoe_stage")
    assert result.returncode == 0
    assert "CANOE-MARK: efisp-verified" in result.stdout
    assert toolkit.device.saw("push:")
    assert toolkit.device.saw("sh /persist/efisp/.canoe.stage/canoe_device_install.sh")
    assert toolkit.device.boot_root.joinpath("boot.efi").read_bytes() == toolkit.read(
        "efisp/boot.efi"
    )
    assert toolkit.device.boot_root.joinpath("boot_backup.efi").read_bytes() == old_boot
    assert toolkit.device.boot_root.joinpath("BOOTENTRIES").read_bytes() == toolkit.read(
        "efisp/BOOTENTRIES"
    )
    assert toolkit.device.boot_root.joinpath("tools/ArbTools.efi").read_bytes() == toolkit.read(
        "efisp/tools/ArbTools.efi"
    )
    assert toolkit.device.efisp.read_bytes()[: len(toolkit.read("BDS.efi"))] == toolkit.read(
        "BDS.efi"
    )
    assert toolkit.root.joinpath("work/efisp-backup.img").read_bytes() == old_efisp

    assert "preferred-mode record was left untouched" in result.stdout
    _clean_stage(toolkit)


def test_stage_verifies_tzmap_when_abl_loader_is_available(toolkit: FakeToolkit) -> None:
    """Given the recorded loader, staging verifies the tzmap before transaction."""
    _prepare_with_loader(toolkit)
    result = toolkit.run("canoe_stage")

    assert result.returncode == 0, result.stderr
    assert "tzmap-verify boot.efi.tzmap ABL_original.efi" in toolkit.trace()


def test_stage_rejects_a_tzmap_digest_mismatch_before_transaction(toolkit: FakeToolkit) -> None:
    """Given a mismatched tzmap, staging stops before the device transaction."""
    _prepare_with_loader(toolkit)
    result = toolkit.run("canoe_stage", STUB_TZMAP="verify")

    assert result.returncode != 0
    assert "abl_tzmap verify failed" in result.stderr
    assert not toolkit.device.saw("sh /persist/efisp/.canoe.stage/canoe_device_install.sh")


def test_stage_skips_tzmap_check_when_abl_loader_is_missing(toolkit: FakeToolkit) -> None:
    """Given a prepared folder without its loader, staging reports an explicit skip."""
    _prepare(toolkit)
    result = toolkit.run("canoe_stage")

    assert result.returncode == 0, result.stderr
    assert "skipping ABL/tzmap consistency check: ABL_original.efi is unavailable" in result.stdout


def test_failed_push_never_invokes_transaction(toolkit: FakeToolkit) -> None:
    """Given a live device, a staged push failure preserves live bytes and aborts."""
    _prepare(toolkit)
    old_boot = toolkit.device.boot_root.joinpath("boot.efi").read_bytes()
    old_efisp = toolkit.device.efisp.read_bytes()
    result = toolkit.run("canoe_stage", STUB_FAIL="boot.efi.tzmap")
    assert result.returncode != 0
    assert toolkit.device.saw("FAULT:")
    assert "CANOE-MARK: committed" not in result.stdout
    assert toolkit.device.boot_root.joinpath("boot.efi").read_bytes() == old_boot
    assert toolkit.device.efisp.read_bytes() == old_efisp
    assert not toolkit.device.saw("sh /persist/efisp/.canoe.stage/canoe_device_install.sh")
    _clean_stage(toolkit)


def test_silent_size_probe_reports_none_and_skips_transaction(
    toolkit: FakeToolkit,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
) -> None:
    """Given a silent device probe, the sentinel is reported instead of cmd garbage."""
    result, error = _run_size_probe(toolkit, monkeypatch, capsys, None)
    assert result == 1
    assert "gm2p did not land as 120 bytes (got none)" in error
    assert "(got  =)" not in error
    assert not toolkit.device.saw("sh /persist/efisp/.canoe.stage/canoe_device_install.sh")


def test_short_size_probe_names_received_length(
    toolkit: FakeToolkit,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
) -> None:
    """Given a short staged sidecar, the exact received length stops the transaction."""
    result, error = _run_size_probe(toolkit, monkeypatch, capsys, 119)
    assert result == 1
    assert "gm2p did not land as 120 bytes (got 119)" in error
    assert not toolkit.device.saw("sh /persist/efisp/.canoe.stage/canoe_device_install.sh")


def test_skip_bds_installs_tree_without_geometry(toolkit: FakeToolkit) -> None:
    """Given --skip-bds, the tree commits while efisp and geometry remain untouched."""
    _prepare(toolkit)
    before = toolkit.device.efisp.read_bytes()
    result = toolkit.run("canoe_stage", "--skip-bds")
    assert result.returncode == 0
    assert toolkit.device.efisp.read_bytes() == before
    assert not toolkit.device.saw("BDS.efi")
    assert not toolkit.device.saw("blockdev --getsize64")
    _clean_stage(toolkit)


def test_mode_sets_record_after_transaction(toolkit: FakeToolkit) -> None:
    """Given --mode 2, the record is written and reread at the partition tail."""
    _prepare(toolkit)
    result = toolkit.run("canoe_stage", "--mode", "2")
    assert result.returncode == 0
    assert "record reread: MODE=2|MODE_DEFAULTED=0" in result.stdout
    assert "Preferred boot mode set to 2." in result.stdout
    record_offset = 4 * 1024 * 1024 - 3072
    record = toolkit.device.efisp.read_bytes()[record_offset : record_offset + 7]
    assert record == b"SFBM1|2"


def test_bad_mode_is_rejected_before_device_work(toolkit: FakeToolkit) -> None:
    """Given an unsupported mode, parsing rejects it with the documented message."""
    result = toolkit.run("canoe_stage", "--mode", "9")
    assert result.returncode != 0
    assert "must be 0, 1 or 2" in result.stderr
    assert toolkit.device.log == ""


def test_mode_rejects_small_and_unaligned_partitions(make_toolkit: ToolkitFactory) -> None:
    """Given invalid efisp geometries, mode setup stops with a precise reason."""
    small = make_toolkit(efisp_bytes=1024 * 1024 - 1)
    odd = make_toolkit(efisp_bytes=4 * 1024 * 1024 + 1)
    _prepare(small)
    _prepare(odd)
    small_result = small.run("canoe_stage", "--mode", "2")
    odd_result = odd.run("canoe_stage", "--mode", "2")
    assert small_result.returncode != 0
    assert "no room for the mode record" in small_result.stderr
    assert odd_result.returncode != 0
    assert "not a multiple of 4096 or 512" in odd_result.stderr


def test_corrupt_bds_rolls_back_and_keeps_host_backup(toolkit: FakeToolkit) -> None:
    """Given a corrupt BDS write, the device transaction rolls back and backup is pulled."""
    _prepare(toolkit)
    old_boot = toolkit.device.boot_root.joinpath("boot.efi").read_bytes()
    old_efisp = toolkit.device.efisp.read_bytes()
    result = toolkit.run("canoe_stage", STUB_CORRUPT="1")
    assert result.returncode != 0
    assert "device-side install failed and rolled back" in result.stderr
    assert "CANOE-MARK: efisp-verified" not in result.stdout
    assert "CANOE-MARK: efisp-restored" in result.stdout
    assert toolkit.device.boot_root.joinpath("boot.efi").read_bytes() == old_boot
    assert toolkit.device.efisp.read_bytes() == old_efisp
    assert toolkit.root.joinpath("work/efisp-backup.img").read_bytes() == old_efisp
    _clean_stage(toolkit)


def test_rollback_is_caught_when_adb_loses_the_exit_status(toolkit: FakeToolkit) -> None:
    """Given an adbd that always exits 0, a rolled-back install is still rejected.

    An adbd without shell protocol v2 does not propagate the remote exit status,
    so `adb shell` reports success no matter what happened. The driver must fall
    back on the device script's own sign-off mark, or it would tell the operator
    the new chain is installed while the old one was restored underneath them.
    """
    _prepare(toolkit)
    old_efisp = toolkit.device.efisp.read_bytes()
    result = toolkit.run("canoe_stage", STUB_CORRUPT="1", STUB_LOSE_EXIT="1")
    assert result.returncode != 0
    assert "never signed off" in result.stderr
    assert "CANOE-MARK: done" not in result.stdout
    assert "CANOE-MARK: efisp-restored" in result.stdout
    assert toolkit.device.efisp.read_bytes() == old_efisp
    assert toolkit.root.joinpath("work/efisp-backup.img").read_bytes() == old_efisp
    _clean_stage(toolkit)


def test_first_install_reports_no_previous_generation(make_toolkit: ToolkitFactory) -> None:
    """Given no live generation, the first install succeeds and says so."""
    toolkit = make_toolkit(live=False)
    _prepare(toolkit)
    result = toolkit.run("canoe_stage")
    assert result.returncode == 0
    assert "No previous generation was present (first install)." in result.stdout
    assert not toolkit.device.boot_root.joinpath("boot_backup.efi").exists()
