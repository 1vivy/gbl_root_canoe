"""End-to-end tests for the package preparation launcher."""

from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from tests.conftest import FakeToolkit


def _package(toolkit: FakeToolkit) -> Path:
    package = toolkit.root / "package"
    package.mkdir()
    (package / "abl.img").write_bytes(b"STOCK-PACKAGE-ABL")
    (package / "vbmeta.img").write_bytes(b"STOCK-PACKAGE-VBMETA")
    (package / "recovery.img").write_bytes(b"STOCK-PACKAGE-RECOVERY")
    return package


def _custom_images(toolkit: FakeToolkit) -> tuple[Path, Path]:
    recovery = toolkit.root / "custom-recovery.img"
    recovery.write_bytes(b"CUSTOM-RECOVERY-PAYLOAD")
    abl = toolkit.root / "vulnerable-abl.img"
    abl.write_bytes(b"VULNERABLE-ABL-PAYLOAD")
    return recovery, abl


def test_prep_grafts_substitutes_and_preserves_stock_backups_on_rerun(toolkit: FakeToolkit) -> None:
    """Given a package, reruns keep the original images in their backups."""
    package = _package(toolkit)
    recovery, abl = _custom_images(toolkit)
    stock_recovery = (package / "recovery.img").read_bytes()
    stock_abl = (package / "abl.img").read_bytes()

    first = toolkit.run(
        "canoe",
        "prep",
        "--pkg",
        str(package),
        "--recovery",
        str(recovery),
        "--abl",
        str(abl),
        "--in-place",
    )
    assert first.returncode == 0, first.stderr
    grafted = toolkit.root / "work" / "grafted_recovery.img"
    assert grafted.stat().st_size == recovery.stat().st_size
    assert (package / "recovery.img").read_bytes() == grafted.read_bytes()
    assert (package / "recovery.img.canoe-orig").read_bytes() == stock_recovery
    assert (package / "abl.img.canoe-orig").read_bytes() == stock_abl
    assert toolkit.read("images/abl.img") == stock_abl
    assert b"STOCK-PACKAGE-ABL" in toolkit.read("efisp/boot.efi")

    second = toolkit.run(
        "canoe",
        "prep",
        "--pkg",
        str(package),
        "--recovery",
        str(recovery),
        "--abl",
        str(abl),
        "--in-place",
    )
    assert second.returncode == 0, second.stderr
    assert (package / "recovery.img.canoe-orig").read_bytes() == stock_recovery
    assert (package / "abl.img.canoe-orig").read_bytes() == stock_abl
    assert "backup already present" in second.stdout


def test_prep_rejects_size_changing_graft(toolkit: FakeToolkit) -> None:
    """Given a port that grows its output, the graft is rejected before derivation."""
    package = _package(toolkit)
    recovery, _ = _custom_images(toolkit)
    result = toolkit.run(
        "canoe",
        "prep",
        "--pkg",
        str(package),
        "--recovery",
        str(recovery),
        STUB_VBMETAPORT="grow",
    )
    assert result.returncode == 1
    assert "grafted recovery changed size" in result.stderr
    assert not (toolkit.root / "efisp" / "boot.efi").exists()


def test_prep_rejects_missing_package(toolkit: FakeToolkit) -> None:
    """Given a nonexistent package path, the launcher names the missing directory."""
    result = toolkit.run("canoe", "prep", "--pkg", str(toolkit.root / "missing"))
    assert result.returncode == 1
    assert "package directory not found" in result.stderr


def test_prep_rejects_missing_package_abl(toolkit: FakeToolkit) -> None:
    """Given a package without abl.img, preparation stops before host tools run."""
    package = _package(toolkit)
    (package / "abl.img").unlink()
    result = toolkit.run("canoe", "prep", "--pkg", str(package))
    assert result.returncode == 1
    assert "package is missing abl.img" in result.stderr


def test_prep_rejects_missing_package_vbmeta(toolkit: FakeToolkit) -> None:
    """Given a package without vbmeta.img, preparation stops before host tools run."""
    package = _package(toolkit)
    (package / "vbmeta.img").unlink()
    result = toolkit.run("canoe", "prep", "--pkg", str(package))
    assert result.returncode == 1
    assert "package is missing vbmeta.img" in result.stderr


def test_prep_rejects_recovery_without_package_recovery(toolkit: FakeToolkit) -> None:
    """Given custom recovery but no stock recovery, official vbmeta cannot be lifted."""
    package = _package(toolkit)
    (package / "recovery.img").unlink()
    recovery, _ = _custom_images(toolkit)
    result = toolkit.run(
        "canoe",
        "prep",
        "--pkg",
        str(package),
        "--recovery",
        str(recovery),
    )
    assert result.returncode == 1
    assert "package is missing recovery.img" in result.stderr
