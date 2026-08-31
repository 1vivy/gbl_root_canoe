"""Tests for USB discovery and direct canoe-ext4 host access."""

from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Literal

import pytest

from canoelib import bootmgr, massstorage
from canoelib.errors import CanoeError
from canoelib.proc import Completed


def test_local_boot_root_accepts_mount_and_efisp_directory(tmp_path: Path) -> None:
    """Given either local path form, retain the same non-owned boot root."""
    mount = tmp_path / "persist"
    mount.mkdir()
    from_mount = massstorage.local_boot_root(mount)

    assert from_mount.boot_root is not None
    from_efisp = massstorage.local_boot_root(Path(from_mount.boot_root))
    assert from_mount.backend == "local"
    assert from_mount.boot_root == mount / "efisp"
    assert from_mount.source is None
    assert from_mount.owned is False
    assert from_efisp.boot_root == from_mount.boot_root


def test_local_boot_root_rejects_file_and_non_directory_efisp(tmp_path: Path) -> None:
    """Given a file at either boundary, refuse it with an operator-facing error."""
    persist_file = tmp_path / "persist-file"
    persist_file.write_bytes(b"not a directory")
    with pytest.raises(CanoeError, match="not a directory"):
        massstorage.local_boot_root(persist_file)

    mount = tmp_path / "persist"
    mount.mkdir()
    (mount / "efisp").write_bytes(b"not a directory")
    with pytest.raises(CanoeError, match="boot root is not a directory"):
        massstorage.local_boot_root(mount)


def _helper_binary() -> Path:
    """Build and return the repository's libext2fs helper."""
    helper_root = Path(__file__).resolve().parents[2] / "canoe-ext4"
    result = subprocess.run(
        ["make", "-C", str(helper_root), "canoe-ext4"],
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    return helper_root / "canoe-ext4"


def _run_helper(
    helper: Path, *args: str, input_bytes: bytes = b""
) -> subprocess.CompletedProcess[bytes]:
    """Invoke one helper operation without a shell or elevated privileges."""
    return subprocess.run(
        [str(helper), *args],
        input=input_bytes,
        capture_output=True,
        check=False,
    )


def test_ext4_helper_reads_writes_and_reads_real_image(tmp_path: Path) -> None:
    """Given a fresh ext4 image, write bytes through the helper and read them back."""
    helper = _helper_binary()
    image = tmp_path / "persist.img"
    image.write_bytes(b"\0" * (32 * 1024 * 1024))
    formatted = subprocess.run(
        ["mke2fs", "-q", "-t", "ext4", "-F", str(image)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert formatted.returncode == 0, formatted.stderr

    created = _run_helper(helper, "--mkdir-p", "mkdir", str(image), "/efisp/tools")
    assert created.returncode == 0, created.stderr.decode()
    payload = b"direct helper payload\n"
    written = _run_helper(
        helper, "write", str(image), "/efisp/tools/marker", input_bytes=payload
    )
    assert written.returncode == 0, written.stderr.decode()
    read_back = _run_helper(helper, "read", str(image), "/efisp/tools/marker")

    assert read_back.returncode == 0, read_back.stderr.decode()
    assert read_back.stdout == payload


def _candidate(
    path: str,
    identity: str | None,
    *,
    mounted_at: str | None = None,
    kind: Literal["block", "image", "dir"] = "block",
) -> bootmgr.SourceCandidate:
    """Build a detector row with the contract's stable defaults."""
    return bootmgr.SourceCandidate(
        kind=kind,
        path=path,
        identity=identity,
        model="Canoe persist",
        size_bytes=128 * 1024 * 1024,
        boot_root="/efisp",
        boot_root_present=True,
        readable=True,
        writable=False,
        needs_privilege=True,
        mounted_at=mounted_at,
        why="test source",
    )


def test_bootmgr_detect_parses_canonical_source_response(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given source.detect JSON, parse every field without a host-side detector."""
    (tmp_path / "bin").mkdir()
    (tmp_path / "bin" / "canoe-bootmgr").write_bytes(b"stub")
    payload = (
        '{"ok":true,"kind":"source.detect","sources":['
        '{"kind":"block","path":"/dev/sda","identity":"1209:ca0e",'
        '"model":"Canoe persist","size_bytes":134217728,"boot_root":"/efisp",'
        '"boot_root_present":true,"readable":true,"writable":false,'
        '"needs_privilege":true,"mounted_at":null,"why":"exported persist LUN"}]}'
    )
    monkeypatch.setattr(bootmgr, "run", lambda command: Completed(0, payload, ""))

    candidates = bootmgr.detect(bootmgr.Toolkit(tmp_path))

    assert len(candidates) == 1
    assert candidates[0].identity == "1209:ca0e"
    assert candidates[0].path == "/dev/sda"
    assert candidates[0].mounted_at is None


def test_detect_export_preserves_identity_and_mount_safety(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given detector rows, reject foreign or mounted sources and select Canoe raw LUN."""
    rows = (
        _candidate("/dev/sda", "1209:ca0e", mounted_at="/run/media/persist"),
        _candidate("/dev/sdb", "9999:0001"),
        _candidate("/dev/sdc", "05c6:f000"),
    )
    monkeypatch.setattr(bootmgr, "detect", lambda toolkit: rows)

    handle = massstorage._detect_export(tmp_path)

    assert handle is not None
    assert handle.source == Path("/dev/sdc")
    assert handle.owned is True


def test_find_export_reports_empty_detector_result(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given no detector candidates, report an actionable no-LUN error."""
    monkeypatch.setattr(bootmgr, "detect", lambda toolkit: ())

    with pytest.raises(CanoeError, match="source detect returned no usable candidate"):
        massstorage._find_export(tmp_path, 0)


def test_export_adopts_live_session_without_reasking_fastboot(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Given a live detector row, return its raw source without fastboot."""
    row = _candidate("/dev/sda", "1209:ca0e")
    monkeypatch.setattr(bootmgr, "detect", lambda toolkit: (row,))

    def refuse(*_: object, **__: object) -> None:
        raise AssertionError("fastboot must not be re-issued during a live export")

    monkeypatch.setattr(massstorage.subprocess, "Popen", refuse)

    handle = massstorage.export(tmp_path)

    assert handle.backend == "ext4"
    assert handle.source == Path("/dev/sda")
    assert handle.node == Path("/dev/sda")
