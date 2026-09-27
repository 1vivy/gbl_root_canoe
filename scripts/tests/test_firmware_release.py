from __future__ import annotations

import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

SPEC = importlib.util.spec_from_file_location("firmware_release", Path(__file__).resolve().parents[1] / "firmware_release.py")
assert SPEC and SPEC.loader
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)
COMMIT = "1" * 40
VERSION = "7.0.0-b5"


def efi(seed: int = 0) -> bytes:
    data = bytearray([seed] * 512)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 64)
    data[64:68] = b"PE\0\0"
    struct.pack_into("<H", data, 68, 0xAA64)
    struct.pack_into("<H", data, 84, 112)
    struct.pack_into("<H", data, 88, 0x20B)
    struct.pack_into("<H", data, 156, 10)
    return bytes(data)


def android_elf(payload: bytes = b"") -> bytes:
    data = bytearray(128)
    data[:4] = b"\x7fELF"
    data[4] = 2
    struct.pack_into("<H", data, 18, 0xB7)
    data[64:64 + len(payload)] = payload
    return bytes(data)


def one_shot_package(artifacts: dict[str, bytes], *, drop: str = "", extra: tuple[str, ...] = (),
                     mode: int | None = None, command: bytes | None = None, bds: bytes | None = None) -> bytes:
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", zipfile.ZIP_DEFLATED) as package:
        def member(name: str, data: bytes, permissions: int) -> None:
            info = zipfile.ZipInfo(name)
            info.external_attr = (stat.S_IFREG | permissions) << 16
            package.writestr(info, data)
        member("BDS.efi", bds if bds is not None else artifacts["BDS.efi"], 0o644)
        member("README.txt", b"one-shot package\n", 0o644)
        member("install-canoe.sh", b"#!/bin/sh\n", mode if mode is not None else 0o755)
        for name in release.ONE_SHOT_COMMANDS:
            member(f"bin/{name}", command if command is not None else android_elf(), 0o755)
        for name in release.ONE_SHOT_TOOLS:
            member(f"efisp/tools/{name}", artifacts[name], 0o644)
        for name in extra:
            member(name, b"extra\n", 0o644)
    if drop:
        with zipfile.ZipFile(io.BytesIO(stream.getvalue())) as original:
            trimmed_stream = io.BytesIO()
            with zipfile.ZipFile(trimmed_stream, "w", zipfile.ZIP_DEFLATED) as trimmed:
                for info in original.infolist():
                    if info.filename != drop:
                        trimmed.writestr(info, original.read(info.filename))
            stream = trimmed_stream
    return stream.getvalue()


def fixture(directory: Path) -> dict:
    artifacts = {name: efi(index) for index, name in enumerate(release.EFI_FILES)}
    package = one_shot_package(artifacts)
    manifest = release.make_manifest(VERSION, COMMIT, artifacts, release.inspect_one_shot(package, VERSION, artifacts))
    for name, data in artifacts.items():
        (directory / name).write_bytes(data)
    (directory / release.one_shot_name(VERSION)).write_bytes(package)
    (directory / "manifest.json").write_text(json.dumps(manifest) + "\n")
    sums = [f"{release.digest(item.read_bytes())}  {item.name}" for item in sorted(directory.iterdir())]
    (directory / "SHA256SUMS").write_text("\n".join(sums) + "\n")
    return manifest


def source_root(directory: Path) -> Path:
    """A checkout-shaped tree with a staged one-shot archive, for package()."""
    (directory / "version.mk").write_text(f"CANOE_VERSION = {VERSION}\n")
    build = directory / "submodules/uefi/build"
    build.mkdir(parents=True)
    artifacts = {name: efi(index) for index, name in enumerate(release.EFI_FILES)}
    for name, data in artifacts.items():
        (build / name).write_bytes(data)
    staged = directory / "targets/one_shot_android/build"
    staged.mkdir(parents=True)
    (staged / release.one_shot_name(VERSION)).write_bytes(one_shot_package(artifacts))
    return directory


class FirmwareReleaseTests(unittest.TestCase):
    def test_package_stages_the_exact_android_archive(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = source_root(Path(temporary))
            source = root / "targets/one_shot_android/build" / release.one_shot_name(VERSION)
            with patch.object(release, "command", side_effect=["", COMMIT]):
                output = release.package(root)
            self.assertEqual((output / source.name).read_bytes(), source.read_bytes())
            self.assertEqual(release.verify(output)["oneShot"]["sha256"], release.digest(source.read_bytes()))

    def test_complete_firmware_catalogue_and_every_raw_hash(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            expected = fixture(root)
            actual = release.verify(root)
            self.assertEqual(actual, expected)
            self.assertEqual(len(actual["tools"]), 8)
            self.assertEqual(actual["tag"], "release-7.0.0-b5")
            self.assertEqual(actual["source"], COMMIT)
            self.assertEqual(actual["sha256"], release.digest((root / "BDS.efi").read_bytes()))
            self.assertEqual(actual["oneShot"]["sha256"], release.digest((root / release.one_shot_name(VERSION)).read_bytes()))

    def test_tampered_tool_and_missing_tool_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); fixture(root)
            (root / "UsbTools.efi").write_bytes(efi(42))
            with self.assertRaises(ValueError):
                release.verify(root)
            (root / "LogTools.efi").unlink()
            with self.assertRaisesRegex(ValueError, "missing or unexpected"):
                release.verify(root)

    def test_non_arm64_payload_and_checksum_metadata_are_rejected(self):
        bad = bytearray(efi()); struct.pack_into("<H", bad, 68, 0x8664)
        with self.assertRaisesRegex(ValueError, "ARM64"):
            release.inspect_efi(bytes(bad), "BDS.efi")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); fixture(root)
            with (root / "SHA256SUMS").open("a") as output:
                output.write("0" * 64 + "  manifest.json\n")
            with self.assertRaisesRegex(ValueError, "repeated"):
                release.verify(root)

    def test_one_shot_rejects_wrong_firmware_and_unsafe_payloads(self):
        artifacts = {name: efi(index) for index, name in enumerate(release.EFI_FILES)}
        with self.assertRaisesRegex(ValueError, "different BDS.efi"):
            release.inspect_one_shot(one_shot_package(artifacts, bds=efi(42)), VERSION, artifacts)
        with self.assertRaisesRegex(ValueError, "ARM64 Android ELF"):
            release.inspect_one_shot(one_shot_package(artifacts, command=b"not an ELF"), VERSION, artifacts)
        with self.assertRaisesRegex(ValueError, "install-canoe.sh.*mode 755"):
            release.inspect_one_shot(one_shot_package(artifacts, mode=0o644), VERSION, artifacts)
        with self.assertRaisesRegex(ValueError, "exactly the installer"):
            release.inspect_one_shot(one_shot_package(artifacts, drop="bin/canoe-provision"), VERSION, artifacts)


    def test_old_and_branch_style_release_identifiers_are_rejected(self):
        for tag in ["7.0.0-b5", "v7.0.0-b5", "main", "release-main"]:
            with self.assertRaisesRegex(ValueError, "release-<version>"):
                release.resolve_tag(tag)
        with patch.object(release, "command", side_effect=[COMMIT, "CANOE_VERSION = 7.0.0-b5\n"]) as invoke:
            self.assertEqual(release.resolve_tag("release-7.0.0-b5"), ("7.0.0-b5", COMMIT))
            self.assertEqual(invoke.call_args_list[0].args[0][-1], "refs/tags/release-7.0.0-b5^{commit}")

    def test_release_upload_remains_draft_and_cannot_replace_published_release(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); fixture(root)
            calls = []
            def invoke(args, *_):
                calls.append(args)
                if args[:3] == ["git", "rev-parse", "HEAD"]:
                    return COMMIT
                if args[:2] == ["gh", "api"]:
                    self.assertIn("--slurp", args)
                    self.assertNotIn("--jq", args)
                    return "[[]]"
                return ""
            with patch.object(release, "resolve_tag", return_value=("7.0.0-b5", COMMIT)), patch.object(release, "command", side_effect=invoke), patch.dict(os.environ, {"GITHUB_REPOSITORY": "fixture/firmware"}):
                release.draft("release-7.0.0-b5", root)
            create = next(args for args in calls if args[:3] == ["gh", "release", "create"])
            self.assertIn("--draft", create)
            self.assertIn("--verify-tag", create)
            self.assertIn("--prerelease", create)
            uploaded = next(args for args in calls if args[:3] == ["gh", "release", "upload"])
            self.assertEqual({Path(name).name for name in uploaded[7:]},
                             set(release.EFI_FILES) | {"manifest.json", "SHA256SUMS", release.one_shot_name(VERSION)})
            with patch.object(release, "resolve_tag", return_value=("7.0.0-b5", COMMIT)), patch.object(release, "command", side_effect=[COMMIT, json.dumps([[], [{"tag_name": "release-7.0.0-b5", "draft": False}]])]) as blocked, patch.dict(os.environ, {"GITHUB_REPOSITORY": "fixture/firmware"}):
                with self.assertRaisesRegex(ValueError, "Published firmware releases are not replaced"):
                    release.draft("release-7.0.0-b5", root)
                self.assertEqual(blocked.call_count, 2)

    def test_existing_draft_on_later_page_is_uploaded_without_recreation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); fixture(root)
            calls = []
            def invoke(args, *_):
                calls.append(args)
                if args[:3] == ["git", "rev-parse", "HEAD"]:
                    return COMMIT
                if args[:2] == ["gh", "api"]:
                    self.assertIn("--paginate", args)
                    self.assertIn("--slurp", args)
                    self.assertNotIn("--jq", args)
                    return json.dumps([[{"tag_name": "older", "draft": False}], [{"tag_name": "release-7.0.0-b5", "draft": True}]])
                return ""
            with patch.object(release, "resolve_tag", return_value=("7.0.0-b5", COMMIT)), patch.object(release, "command", side_effect=invoke), patch.dict(os.environ, {"GITHUB_REPOSITORY": "fixture/firmware"}):
                release.draft("release-7.0.0-b5", root)
            self.assertFalse(any(args[:3] == ["gh", "release", "create"] for args in calls))
            self.assertTrue(any(args[:3] == ["gh", "release", "upload"] for args in calls))

    def test_wrong_source_is_rejected_before_any_github_command(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); fixture(root)
            with patch.object(release, "resolve_tag", return_value=("7.0.0-b5", "2" * 40)), patch.object(release, "command") as invoke:
                with self.assertRaisesRegex(ValueError, "do not belong to the selected tag"):
                    release.draft("release-7.0.0-b5", root)
                invoke.assert_not_called()


if __name__ == "__main__":
    unittest.main()
