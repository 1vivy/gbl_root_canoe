"""Per-kind checks and generated make rendering for imported rows."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path
from typing import assert_never
from imports_model import load_manifest, sha256


def check_artifact(root: Path, row: dict) -> tuple[str, str, bool]:
    path = root / row["path"]
    if not path.is_file():
        return "FAIL", f"{row['id']}: artifact missing: {row['path']}", True
    actual_size = path.stat().st_size
    actual_digest = sha256(path)
    expected_digest = row["sha256"]
    expected_size = row["bytes"]
    if actual_digest != expected_digest or actual_size != expected_size:
        return "FAIL", (f"{row['id']}: artifact mismatch: expected sha256={expected_digest} expected bytes={expected_size}; "
                         f"actual sha256={actual_digest} actual bytes={actual_size}"), True
    return "PASS", f"{row['id']}: artifact digest matches expected ({actual_size} bytes)", False


def check_fetch(row: dict) -> tuple[str, str, bool]:
    if row.get("sha256") is None:
        return "UNPINNED", f"{row['id']}: fetch is UNPINNED (no upstream digest recorded)", False
    return "PASS", f"{row['id']}: fetch pin is complete", False


def check_subtree(root: Path, row: dict) -> tuple[str, str, bool]:
    result = subprocess.run(
        ["git", "diff", "--name-only", row["imported_at"], "HEAD", "--", row["path"]],
        cwd=root, text=True, capture_output=True, check=False,
    )
    if result.returncode != 0:
        return "FAIL", f"{row['id']}: git diff failed: {result.stderr.strip()}", True
    excluded = set(row.get("exclude", []))
    changed = []
    subtree = Path(row["path"])
    for name in result.stdout.splitlines():
        try:
            relative = Path(name).relative_to(subtree)
        except ValueError:
            continue
        if any(component in excluded for component in relative.parts):
            continue
        changed.append(name)
    if not changed:
        return "PASS", f"{row['id']}: subtree is unchanged outside excludes", False
    allowed = " (local patches allowed)" if row["local_patches"] else " (local_patches=false)"
    return "DRIFT", f"{row['id']}: DRIFT: {len(changed)} file(s){allowed}", not row["local_patches"]


def read_meta(path: Path) -> dict[str, str]:
    """Read the digest and size keys from a data-entry metadata file."""
    values: dict[str, str] = {}
    for line in path.read_text().splitlines():
        match = re.match(r"^\s*(sha256|bytes)\s*=\s*(\S+)\s*$", line)
        if match:
            values[match[1]] = match[2]
    return values


def check_data(root: Path, row: dict) -> tuple[str, str, bool]:
    base = root / row["path"]
    if not base.is_dir():
        return "FAIL", f"{row['id']}: data directory missing: {row['path']}", True
    failures: list[str] = []
    for entry in sorted(base.iterdir()):
        if not entry.is_dir():
            continue
        image = entry / row["entry_image"]
        digest_file = entry / row["entry_digest"]
        if not image.is_file() or not digest_file.is_file():
            failures.append(f"{entry.name} missing image or digest")
            continue
        actual_digest = sha256(image)
        actual_size = image.stat().st_size
        recorded = digest_file.read_text().strip().split()
        expected_digest = recorded[0] if recorded else "<missing>"
        if expected_digest != actual_digest:
            failures.append(f"{entry.name} digest expected {expected_digest} actual {actual_digest}")
        if row.get("entry_meta"):
            meta = entry / row["entry_meta"]
            if not meta.is_file():
                failures.append(f"{entry.name} metadata missing")
            else:
                values = read_meta(meta)
                if values.get("sha256") != actual_digest:
                    failures.append(f"{entry.name} meta sha256 disagrees")
                if values.get("bytes") != str(actual_size):
                    failures.append(f"{entry.name} meta bytes expected {actual_size} actual {values.get('bytes', '<missing>')}")
    if failures:
        return "FAIL", f"{row['id']}: data disagreement: " + "; ".join(failures), True
    return "PASS", f"{row['id']}: data entries agree", False


def check_satellite(root: Path, row: dict) -> tuple[str, str, bool]:
    failures: list[str] = []
    base = root / row["path"]
    for name, expected in row["versions"].items():
        path = base / name
        if not path.is_file():
            failures.append(f"{name} missing (expected {expected})")
            continue
        actual = "<missing>"
        for line in path.read_text().splitlines():
            match = re.match(rf"^\s*{re.escape(row['version_key'])}\s*=\s*(\S+)", line)
            if match:
                actual = match[1].strip('"')
                break
        if actual != expected:
            failures.append(f"{name} expected {expected} actual {actual}")
    if failures:
        return "FAIL", f"{row['id']}: satellite version drift: " + "; ".join(failures), True
    return "PASS", f"{row['id']}: satellite versions agree", False


def evaluate(root: Path, row: dict) -> tuple[str, str, bool]:
    """Evaluate one parsed row and return status, detail, and failure flag."""
    kind = row["kind"]
    match kind:
        case "artifact":
            return check_artifact(root, row)
        case "fetch":
            return check_fetch(row)
        case "subtree":
            return check_subtree(root, row)
        case "data":
            return check_data(root, row)
        case "satellite":
            return check_satellite(root, row)
        case "external":
            return "EXTERNAL", f"{row['id']}: EXTERNAL ({row['note']})", False
        case unreachable:
            assert_never(unreachable)


def make_text(root: Path) -> str:
    """Render tracked make variables from manifest rows."""
    rows = load_manifest(root)
    output = ["# Generated by python3 scripts/imports.py mk; do not edit by hand.",
              "# Regenerate with make bump or make import-pin ID=<id>.", ""]
    for row in rows:
        prefix = row.get("make_prefix")
        if prefix is None:
            continue
        output.append(f"{prefix}_VERSION = {row.get('version', '')}")
        output.append(f"{prefix}_SHA256 = {row.get('sha256', '')}")
        output.append(f"{prefix}_URL = {row.get('url', '')}")
        if "path" in row:
            path = row["path"] if str(row["path"]).startswith("/") else f"$(CANOE_ROOT_DIR)/{row['path']}"
            output.append(f"{prefix}_PATH = {path}")
        output.append("")
    return "\n".join(output)


def check(root: Path) -> int:
    """Print all row checks and return the check exit status."""
    rows = load_manifest(root)
    failures = 0
    for row in rows:
        status, message, failed = evaluate(root, row)
        print(f"{status} {message}")
        failures += failed
    generated = root / "imports.mk"
    if generated.exists() and generated.read_text() != make_text(root):
        print("FAIL imports.mk: generated file is stale or hand-edited")
        failures += 1
    print(f"Import check summary: {len(rows)} rows, {failures} failure(s)")
    return 1 if failures else 0


def list_rows(root: Path) -> int:
    """Print one identity and current state line for each row."""
    rows = load_manifest(root)
    for row in rows:
        status, _, _ = evaluate(root, row)
        kind = row["kind"]
        upstream = row.get("upstream", {})
        identity = row.get("version") or upstream.get("rev") or row.get("sha256", "")[:12]
        if not identity:
            identity = row.get("path_var") or "unversioned"
        if kind == "data":
            base = root / row["path"]
            # Count entry directories only: the manifest's data check iterates
            # directories, so counting every child would report a number the
            # check never validated - `ablrepo/README.md` is not an entry.
            entries = [child for child in base.iterdir() if child.is_dir()] if base.is_dir() else []
            identity = f"{len(entries)} entries"
        print(f"{row['id']} {kind} {identity} {status}")
    return 0
