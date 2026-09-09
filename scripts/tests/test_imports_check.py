from __future__ import annotations

import hashlib
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "imports.py"
def run_tool(root: Path, *args: str) -> subprocess.CompletedProcess[str]:
    for name in ("imports.py", "imports_check.py", "imports_model.py", "imports_pin.py"):
        source = ROOT / "scripts" / name
        target = root / "scripts" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source.read_bytes())
    return subprocess.run(
        ["python3", str(root / "scripts" / "imports.py"), *args],
        cwd=root,
        text=True,
        capture_output=True,
        check=False,
    )


def write_manifest(root: Path, body: str) -> None:
    (root / "imports.toml").write_text("schema = 1\n\n" + body)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class ImportCheckTests(unittest.TestCase):
    def test_artifact_mismatch_names_digest_and_sizes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifact = root / "artifact.bin"
            artifact.write_bytes(b"actual")
            write_manifest(
                root,
                f'''[[import]]
id = "sample"
kind = "artifact"
why = "fixture artifact"
path = "artifact.bin"
sha256 = "{'0' * 64}"
bytes = 99
''',
            )
            result = run_tool(root, "check")
            self.assertEqual(result.returncode, 1)
            output = result.stdout + result.stderr
            self.assertIn("expected sha256", output)
            self.assertIn("actual sha256", output)
            self.assertIn("expected bytes=99", output)
            self.assertIn("actual bytes=6", output)

    def test_missing_artifact_fails(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_manifest(
                root,
                '''[[import]]
id = "missing"
kind = "artifact"
why = "fixture artifact"
path = "missing.bin"
sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
bytes = 3
''',
            )
            result = run_tool(root, "check")
            self.assertEqual(result.returncode, 1)
            self.assertIn("missing.bin", result.stdout)

    def test_unpinned_fetch_reports_without_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_manifest(
                root,
                '''[[import]]
id = "source"
kind = "fetch"
why = "fixture source"
url = "https://example.invalid/source.tar.gz"
pinned = false
''',
            )
            result = run_tool(root, "check")
            self.assertEqual(result.returncode, 0)
            self.assertIn("UNPINNED", result.stdout)

    def test_subtree_drift_respects_exclude_and_local_patches(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            subprocess.run(["git", "config", "user.email", "test@example.invalid"], cwd=root, check=True)
            subprocess.run(["git", "config", "user.name", "Test"], cwd=root, check=True)
            (root / "tree" / "kept").mkdir(parents=True)
            (root / "tree" / "ignored").mkdir()
            (root / "tree" / "kept" / "source.c").write_text("old\n")
            (root / "tree" / "ignored" / "generated.c").write_text("old\n")
            subprocess.run(["git", "add", "tree"], cwd=root, check=True)
            subprocess.run(["git", "commit", "-q", "-m", "import"], cwd=root, check=True)
            imported_at = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
            (root / "tree" / "kept" / "source.c").write_text("new\n")
            (root / "tree" / "ignored" / "generated.c").write_text("new\n")
            subprocess.run(["git", "add", "tree"], cwd=root, check=True)
            subprocess.run(["git", "commit", "-q", "-m", "patch"], cwd=root, check=True)
            write_manifest(
                root,
                f'''[[import]]
id = "patched"
kind = "subtree"
why = "fixture subtree"
path = "tree"
exclude = ["ignored"]
imported_at = "{imported_at}"
local_patches = true
[import.upstream]
url = "https://example.invalid/tree"
rev = "unknown"
''',
            )
            result = run_tool(root, "check")
            self.assertEqual(result.returncode, 0)
            self.assertIn("DRIFT patched", result.stdout)
            self.assertIn("1 file", result.stdout)
            self.assertNotIn("generated.c", result.stdout)

    def test_subtree_drift_fails_without_local_patches(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            subprocess.run(["git", "config", "user.email", "test@example.invalid"], cwd=root, check=True)
            subprocess.run(["git", "config", "user.name", "Test"], cwd=root, check=True)
            (root / "tree").mkdir()
            (root / "tree" / "source.c").write_text("old\n")
            subprocess.run(["git", "add", "tree"], cwd=root, check=True)
            subprocess.run(["git", "commit", "-q", "-m", "import"], cwd=root, check=True)
            imported_at = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
            (root / "tree" / "source.c").write_text("new\n")
            subprocess.run(["git", "add", "tree"], cwd=root, check=True)
            subprocess.run(["git", "commit", "-q", "-m", "patch"], cwd=root, check=True)
            write_manifest(
                root,
                f'''[[import]]
id = "unpatched"
kind = "subtree"
why = "fixture subtree"
path = "tree"
imported_at = "{imported_at}"
local_patches = false
[import.upstream]
url = "https://example.invalid/tree"
rev = "unknown"
''',
            )
            result = run_tool(root, "check")
            self.assertEqual(result.returncode, 1)
            self.assertIn("DRIFT unpatched", result.stdout)

    def test_data_digest_and_meta_disagreement_names_entry(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            entry = root / "data" / "phone"
            entry.mkdir(parents=True)
            image = b"image"
            (entry / "abl.img").write_bytes(image)
            (entry / "abl.sha256").write_text(f"{'0' * 64}  abl.img\n")
            (entry / "abl.meta").write_text(f"sha256={digest(image)}\nbytes=99\n")
            write_manifest(
                root,
                '''[[import]]
id = "dataset"
kind = "data"
why = "fixture data"
path = "data"
entry_image = "abl.img"
entry_digest = "abl.sha256"
entry_meta = "abl.meta"
''',
            )
            result = run_tool(root, "check")
            self.assertEqual(result.returncode, 1)
            self.assertIn("phone", result.stdout)
            self.assertIn("digest", result.stdout)
            self.assertIn("bytes", result.stdout)

    def test_satellite_version_drift_fails(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "satellite" / "tool.inf"
            target.parent.mkdir()
            target.write_text("VERSION_STRING = 0.1\n")
            write_manifest(
                root,
                '''[[import]]
id = "tools"
kind = "satellite"
why = "fixture satellite"
path = "satellite"
version_key = "VERSION_STRING"
[import.versions]
"tool.inf" = "0.2"
''',
            )
            result = run_tool(root, "check")
            self.assertEqual(result.returncode, 1)
            self.assertIn("tool.inf", result.stdout)
            self.assertIn("expected 0.2", result.stdout)


if __name__ == "__main__":
    unittest.main()
