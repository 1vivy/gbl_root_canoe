"""End-to-end tests for the `canoe build` subcommand."""

from __future__ import annotations

import hashlib
from typing import TYPE_CHECKING

import pytest

if TYPE_CHECKING:
    from tests.conftest import FakeToolkit, ToolkitFactory

GM2P_BYTES = 120
TZMAP_BYTES = 256


def _assert_triplet_absent(toolkit: FakeToolkit) -> None:
    """The three outputs must be removed together after every failed derive."""
    for relative in ("efisp/boot.efi", "efisp/boot.efi.gm2p", "efisp/boot.efi.tzmap"):
        assert not (toolkit.root / relative).exists()


def _plant_images(toolkit: FakeToolkit) -> None:
    """Provide the two source images omitted from the generic toolkit fixture."""
    images = toolkit.root / "images"
    (images / "abl.img").write_bytes(b"ABL-IMAGE")
    (images / "vbmeta.img").write_bytes(b"VBMETA-IMAGE")


def test_build_success_derives_and_validates_the_matching_triplet(toolkit: FakeToolkit) -> None:
    # Given a complete fixture toolkit:
    _plant_images(toolkit)
    # When the real launcher runs:
    result = toolkit.run("canoe", "build")

    # Then the matching triplet has the contract sizes and the tools ran in order.
    assert result.returncode == 0, result.stderr
    assert toolkit.read("efisp/boot.efi")
    assert len(toolkit.read("efisp/boot.efi.gm2p")) == GM2P_BYTES
    assert len(toolkit.read("efisp/boot.efi.tzmap")) == TZMAP_BYTES
    assert toolkit.trace() == [
        "extractfv",
        "patch_abl",
        "derive vbmeta.img",
        "validate boot.efi.gm2p",
        "tzmap-derive ABL_original.efi allow=1",
        "tzmap-validate boot.efi.tzmap",
        "tzmap-verify boot.efi.tzmap ABL_original.efi",
    ]
    assert "fastboot flash efisp BDS.efi" in result.stdout
    assert "fastboot flash abl <vulnerable>.img" in result.stdout


def test_build_tzmap_digest_mismatch_names_both_digests(toolkit: FakeToolkit) -> None:
    # Given a complete fixture toolkit and a verifier that detects a mismatch:
    _plant_images(toolkit)
    # When the real launcher runs:
    result = toolkit.run("canoe", "build", STUB_TZMAP="verify")

    # Then the failure identifies both values that were compared.
    assert result.returncode != 0
    sidecar_digest = hashlib.sha256(b"T" * TZMAP_BYTES).hexdigest()
    abl_digest = hashlib.sha256(b"LOADER-FROM-ABL-IMAGE").hexdigest()
    assert f"sidecar={sidecar_digest}" in result.stderr
    assert f"abl={abl_digest}" in result.stderr
    _assert_triplet_absent(toolkit)



@pytest.mark.parametrize(
    ("env", "missing", "stage"),
    [
        pytest.param({"STUB_PATCH": "fail"}, None, "patch_abl failed", id="patch"),
        pytest.param(
            {"STUB_PROFILE": "derive"}, None, "mode2_profile derive failed", id="profile-derive"
        ),
        pytest.param(
            {"STUB_PROFILE": "missing-validate"},
            None,
            "mode2_profile validate failed",
            id="profile-validate",
        ),
        pytest.param({"STUB_PROFILE": "size"}, None, "mode2_profile output", id="profile-size"),
        pytest.param({"STUB_TZMAP": "derive"}, None, "abl_tzmap derive failed", id="tzmap-derive"),
        pytest.param(
            {"STUB_TZMAP": "missing-validate"},
            None,
            "abl_tzmap validate failed",
            id="tzmap-validate",
        ),
        pytest.param(
            {"STUB_TZMAP": "missing-verify"},
            None,
            "abl_tzmap verify failed",
            id="tzmap-verify",
        ),
        pytest.param({"STUB_TZMAP": "verify"}, None, "abl_tzmap verify failed", id="tzmap-digest"),
        pytest.param({"STUB_TZMAP": "size"}, None, "abl_tzmap output", id="tzmap-size"),
        pytest.param({}, "images/abl.img", "extractfv failed", id="missing-abl"),
    ],
)
def test_build_failure_removes_stale_triplet(
    make_toolkit: ToolkitFactory,
    env: dict[str, str],
    missing: str | None,
    stage: str,
) -> None:
    # Given a stale previous generation and an injected failing stage:
    toolkit = make_toolkit()
    _plant_images(toolkit)
    toolkit.plant_triplet()
    if missing is not None:
        (toolkit.root / missing).unlink()

    # When the real launcher runs:
    result = toolkit.run("canoe", "build", **env)

    # Then it names the stage and leaves no stale or partial generation.
    assert result.returncode != 0
    assert result.stderr.startswith("canoe build: error: ")
    assert stage in result.stderr
    _assert_triplet_absent(toolkit)


def test_build_missing_vbmeta_is_the_cheapest_guard(make_toolkit: ToolkitFactory) -> None:
    # Given no matching vbmeta image and a stale previous generation:
    toolkit = make_toolkit()
    _plant_images(toolkit)
    toolkit.plant_triplet()
    (toolkit.root / "images/vbmeta.img").unlink()

    # When the real launcher runs:
    result = toolkit.run("canoe", "build")

    # Then the pair check fails before extractfv and clears every output.
    assert result.returncode != 0
    assert result.stderr.startswith("canoe build: error: ")
    assert "matching images/vbmeta.img is required" in result.stderr
    assert toolkit.trace() == []
    _assert_triplet_absent(toolkit)


@pytest.mark.parametrize("env", [{}, {"STUB_NO_GBL": "1"}], ids=["gbl-patched", "gbl-missing"])
def test_build_report_describes_gbl_downgrade_flow(
    toolkit: FakeToolkit,
    env: dict[str, str],
) -> None:
    # Given either a successfully patched GBL or the documented downgrade path:
    _plant_images(toolkit)
    # When the real launcher runs:
    result = toolkit.run("canoe", "build", **env)

    # Then the report structure has three or four manual steps as appropriate.
    assert result.returncode == 0, result.stderr
    if env:
        assert "WARNING: No GBL exploit found" in result.stdout
        assert "警告：" in result.stdout
        assert "\n4. Flash the bootloader bundle from the host:" in result.stdout
        assert "\n4. 在主机上刷入引导加载程序套件：" in result.stdout
    else:
        assert "WARNING: No GBL exploit found" not in result.stdout
        assert "\n4." not in result.stdout
        assert "\n3. Flash the bootloader bundle from the host:" in result.stdout
        assert "\n3. 在主机上刷入引导加载程序套件：" in result.stdout
