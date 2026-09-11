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


class ImportPinTests(unittest.TestCase):
    def test_pin_rewrites_only_target_row(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "new.bin").write_bytes(b"new bytes")
            manifest = root / "imports.toml"
            manifest.write_text(
                '''schema = 1

[[import]]
id = "first"
kind = "artifact"
why = "first fixture"
path = "old.bin"
sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
bytes = 1

[[import]]
id = "second"
kind = "artifact"
why = "second fixture"
path = "new.bin"
sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
bytes = 1
''',
            )
            original = manifest.read_text()
            manifest.write_text(original.replace('path = "old.bin"', 'path = "new.bin"'))
            before = manifest.read_text().splitlines()
            result = run_tool(root, "pin", "first")
            self.assertEqual(result.returncode, 0, result.stderr)
            after = manifest.read_text().splitlines()
            self.assertEqual(
                [line for line in after if line.startswith("id = \"second\"") or line.startswith("why = \"second") or line.startswith("path = \"new.bin\"")],
                [line for line in before if line.startswith("id = \"second\"") or line.startswith("why = \"second") or line.startswith("path = \"new.bin\"")],
            )
            expected = hashlib.sha256(b"new bytes").hexdigest()
            self.assertIn(f'sha256 = "{expected}"', after)
            self.assertIn("bytes = 9", after)
            self.assertIn("0000000000000000000000000000000000000000000000000000000000000000", "\n".join(after))
            self.assertIn("sha256", result.stdout)

    def test_pin_version_updates_path_and_url(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifact = root / "bundle-1.0.0.tar.gz"
            artifact.write_bytes(b"bundle")
            manifest = root / "imports.toml"
            manifest.write_text(
                '''schema = 1

[[import]]
id = "bundle"
kind = "artifact"
why = "versioned fixture"
path = "bundle-1.0.0.tar.gz"
version = "1.0.0"
url = "https://example.invalid/bundle-1.0.0.tar.gz"
sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
bytes = 1
''',
            )
            result = run_tool(root, "pin", "bundle", "--version", "2.0.0")
            self.assertEqual(result.returncode, 0, result.stderr)
            content = manifest.read_text()
            self.assertIn('path = "bundle-2.0.0.tar.gz"', content)
            self.assertIn('version = "2.0.0"', content)
            self.assertIn('url = "https://example.invalid/bundle-2.0.0.tar.gz"', content)
            self.assertIn("1.0.0 -> 2.0.0", result.stdout)

    def test_mk_output_is_stable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "imports.toml").write_text(
                '''schema = 1

[[import]]
id = "web"
kind = "artifact"
why = "web fixture"
path = "web-1.0.tar.gz"
version = "1.0"
url = "file://$(CANOE_ROOT_DIR)/web-1.0.tar.gz"
sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
bytes = 7
make_prefix = "WEB"

[[import]]
id = "source"
kind = "fetch"
why = "source fixture"
url = "https://example.invalid/source.tar.gz"
sha256 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
version = "3"
make_prefix = "SOURCE"
''',
            )
            first = run_tool(root, "mk")
            second = run_tool(root, "mk")
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(first.stdout, second.stdout)
            self.assertIn("WEB_PATH = $(CANOE_ROOT_DIR)/web-1.0.tar.gz", first.stdout)
            self.assertIn("SOURCE_SHA256 = bbbbb", first.stdout)


if __name__ == "__main__":
    unittest.main()
