from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("firmware_release", Path(__file__).resolve().parents[1] / "firmware_release.py")
assert SPEC and SPEC.loader
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)
COMMIT = "1" * 40


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


def fixture(directory: Path) -> dict:
    artifacts = {name: efi(index) for index, name in enumerate(release.EFI_FILES)}
    manifest = release.make_manifest("7.0.0-b5", COMMIT, artifacts)
    for name, data in artifacts.items():
        (directory / name).write_bytes(data)
    (directory / "manifest.json").write_text(json.dumps(manifest) + "\n")
    sums = [f"{release.digest(item.read_bytes())}  {item.name}" for item in sorted(directory.iterdir())]
    (directory / "SHA256SUMS").write_text("\n".join(sums) + "\n")
    return manifest


class FirmwareReleaseTests(unittest.TestCase):
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

    def test_tampered_tool_and_missing_tool_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); fixture(root)
            (root / "UsbTools.efi").write_bytes(efi(42))
            with self.assertRaisesRegex(ValueError, "differ from their manifest"):
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
                    return "[]"
                return ""
            with patch.object(release, "resolve_tag", return_value=("7.0.0-b5", COMMIT)), patch.object(release, "command", side_effect=invoke), patch.dict(os.environ, {"GITHUB_REPOSITORY": "fixture/firmware"}):
                release.draft("release-7.0.0-b5", root)
            create = next(args for args in calls if args[:3] == ["gh", "release", "create"])
            self.assertIn("--draft", create)
            self.assertIn("--verify-tag", create)
            self.assertIn("--prerelease", create)
            uploaded = next(args for args in calls if args[:3] == ["gh", "release", "upload"])
            self.assertEqual({Path(name).name for name in uploaded[7:]}, set(release.EFI_FILES) | {"manifest.json", "SHA256SUMS"})
            with patch.object(release, "resolve_tag", return_value=("7.0.0-b5", COMMIT)), patch.object(release, "command", side_effect=[COMMIT, json.dumps([{"tag_name": "release-7.0.0-b5", "draft": False}])]) as blocked, patch.dict(os.environ, {"GITHUB_REPOSITORY": "fixture/firmware"}):
                with self.assertRaisesRegex(ValueError, "Published firmware releases are not replaced"):
                    release.draft("release-7.0.0-b5", root)
                self.assertEqual(blocked.call_count, 2)

    def test_wrong_source_is_rejected_before_any_github_command(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); fixture(root)
            with patch.object(release, "resolve_tag", return_value=("7.0.0-b5", "2" * 40)), patch.object(release, "command") as invoke:
                with self.assertRaisesRegex(ValueError, "do not belong to the selected tag"):
                    release.draft("release-7.0.0-b5", root)
                invoke.assert_not_called()


if __name__ == "__main__":
    unittest.main()
