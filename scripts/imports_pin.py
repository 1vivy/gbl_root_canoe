"""Anchored in-place pinning for imported artifacts."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

from imports_model import PinError, load_manifest, sha256

def set_line(lines: list[str], key: str, value: str) -> None:
    """Replace one anchored TOML key line, or insert it after id."""
    pattern = re.compile(rf"^(\s*{re.escape(key)}\s*=\s*)(.*?)(\r?\n)?$")
    rendered = value if key == "bytes" else json.dumps(value)
    for index, line in enumerate(lines):
        match = pattern.match(line)
        if match:
            newline = match[3] or ""
            comment = ""
            if "#" in match[2]:
                comment = match[2][match[2].index("#"):]
            suffix = f" {comment}" if comment else ""
            lines[index] = f"{match[1]}{rendered}{suffix}{newline}"
            return
    id_index = next((index for index, line in enumerate(lines) if re.match(r"^\s*id\s*=", line)), None)
    if id_index is None:
        raise PinError(f"target row has no id while rewriting {key}")
    lines.insert(id_index + 1, f"{key} = {rendered}\n")


def pin(root: Path, row_id: str, version: str | None) -> int:
    """Hash one artifact and update only its TOML table."""
    rows = load_manifest(root)
    target = next((row for row in rows if row["id"] == row_id), None)
    if target is None:
        raise PinError(f"unknown import id: {row_id}")
    if target["kind"] != "artifact":
        raise PinError(f"{row_id}: kind {target['kind']} has no local file to hash")
    if version is not None and not re.fullmatch(r"[0-9A-Za-z][0-9A-Za-z._+-]*", version):
        raise PinError(f"{row_id}: invalid version {version!r}")
    path = root / target["path"]
    if not path.is_file():
        print(f"{row_id}: artifact missing: {target['path']}", file=sys.stderr)
        return 1
    new_digest = sha256(path)
    new_size = str(path.stat().st_size)
    manifest_path = root / "imports.toml"
    lines = manifest_path.read_text().splitlines(keepends=True)
    starts = [index for index, line in enumerate(lines) if re.match(r"^\s*\[\[import\]\](?:\s*#.*)?$", line.rstrip("\r\n"))]
    id_pattern = re.compile(r"^\s*id\s*=\s*(['\"])([a-z0-9-]+)\1(?:\s*#.*)?$")
    matching = []
    for position, start in enumerate(starts):
        end = starts[position + 1] if position + 1 < len(starts) else len(lines)
        if any((match := id_pattern.match(line.rstrip("\r\n"))) is not None and match[2] == row_id for line in lines[start:end]):
            matching.append((start, end))
    if len(matching) != 1:
        raise PinError(f"{row_id}: expected exactly one anchored table, found {len(matching)}")
    start, end = matching[0]
    block = lines[start:end]
    old_digest = target["sha256"]
    old_size = str(target["bytes"])
    set_line(block, "sha256", new_digest)
    set_line(block, "bytes", new_size)
    version_message = ""
    if version is not None:
        old_version = target.get("version")
        if not isinstance(old_version, str) or not old_version:
            raise PinError(f"{row_id}: --version requires an existing version template")
        for key in ("path", "url"):
            if key in target:
                old_value = target[key]
                if old_value.count(old_version) != 1:
                    raise PinError(f"{row_id}: cannot safely update {key}; version is not a unique substring")
                set_line(block, key, old_value.replace(old_version, version))
        set_line(block, "version", version)
        version_message = f", version {old_version} -> {version}"
    lines[start:end] = block
    manifest_path.write_bytes("".join(lines).encode())
    print(f"Pinned {row_id}: sha256 {old_digest} -> {new_digest}, bytes {old_size} -> {new_size}{version_message}")
    return 0
