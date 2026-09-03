"""Shared manifest parsing and file helpers for the imports CLI."""

from __future__ import annotations

import hashlib
import re
import tomllib
from pathlib import Path
from typing import assert_never

SCHEMA = 1
KINDS = {"artifact", "fetch", "subtree", "data", "satellite", "external"}
HEX64 = re.compile(r"^[0-9a-fA-F]{64}$")
ID = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")


class ManifestError(Exception):
    """The manifest is malformed or cannot be parsed."""


class PinError(Exception):
    """A pin request cannot be performed safely."""


def require_string(row: dict, key: str) -> str:
    """Return a required string field or reject the manifest."""
    value = row.get(key)
    if not isinstance(value, str) or not value:
        raise ManifestError(f"{row['id']}: {key} is required")
    return value


def validate_row(row: dict) -> None:
    """Validate fields that are fixed by the manifest contract."""
    kind = row["kind"]
    row_id = row["id"]
    match kind:
        case "artifact":
            require_string(row, "path")
            sha = require_string(row, "sha256")
            if not HEX64.fullmatch(sha) or not isinstance(row.get("bytes"), int) or row["bytes"] < 0:
                raise ManifestError(f"{row_id}: artifact needs a 64-digit sha256 and non-negative bytes")
        case "fetch":
            require_string(row, "url")
            sha = row.get("sha256")
            if sha is not None and (not isinstance(sha, str) or not HEX64.fullmatch(sha)):
                raise ManifestError(f"{row_id}: fetch sha256 must be 64 hexadecimal digits")
            if sha is None and row.get("pinned") is not False:
                raise ManifestError(f"{row_id}: undigested fetch rows must set pinned = false")
        case "subtree":
            require_string(row, "path")
            upstream = row.get("upstream")
            if not isinstance(upstream, dict):
                raise ManifestError(f"{row_id}: upstream table is required")
            require_string(upstream, "url")
            require_string(upstream, "rev")
            require_string(row, "imported_at")
            if not isinstance(row.get("local_patches"), bool):
                raise ManifestError(f"{row_id}: local_patches is required")
            excludes = row.get("exclude", [])
            if not isinstance(excludes, list) or not all(isinstance(item, str) for item in excludes):
                raise ManifestError(f"{row_id}: exclude must be a list of strings")
        case "data":
            for key in ("path", "entry_image", "entry_digest"):
                require_string(row, key)
            if "entry_meta" in row and not isinstance(row["entry_meta"], str):
                raise ManifestError(f"{row_id}: entry_meta must be a string")
        case "satellite":
            require_string(row, "path")
            require_string(row, "version_key")
            versions = row.get("versions")
            if not isinstance(versions, dict) or not all(isinstance(k, str) and isinstance(v, str) for k, v in versions.items()):
                raise ManifestError(f"{row_id}: versions must map file paths to strings")
        case "external":
            require_string(row, "path_var")
            producer = row.get("producer")
            if not isinstance(producer, dict):
                raise ManifestError(f"{row_id}: producer table is required")
            require_string(producer, "repo")
            require_string(producer, "command")
            require_string(row, "note")
        case unreachable:
            assert_never(unreachable)
    prefix = row.get("make_prefix")
    if prefix is not None and (not isinstance(prefix, str) or not re.fullmatch(r"[A-Z][A-Z0-9_]*", prefix)):
        raise ManifestError(f"{row_id}: make_prefix must be an uppercase make variable prefix")


def load_manifest(root: Path) -> list[dict]:
    """Parse and validate imports.toml at the repository root."""
    path = root / "imports.toml"
    try:
        with path.open("rb") as stream:
            document = tomllib.load(stream)
    except FileNotFoundError as exc:
        raise ManifestError(f"manifest missing: {path}") from exc
    except tomllib.TOMLDecodeError as exc:
        raise ManifestError(f"manifest parse error: {exc}") from exc
    if document.get("schema") != SCHEMA or not isinstance(document.get("import"), list):
        raise ManifestError("manifest requires schema = 1 and [[import]] rows")
    rows = document["import"]
    seen: set[str] = set()
    for row in rows:
        if not isinstance(row, dict):
            raise ManifestError("each import row must be a table")
        row_id = row.get("id")
        kind = row.get("kind")
        if not isinstance(row_id, str) or not ID.fullmatch(row_id) or row_id in seen:
            raise ManifestError(f"invalid or duplicate import id: {row_id!r}")
        if not isinstance(kind, str) or kind not in KINDS:
            raise ManifestError(f"{row_id}: unknown kind {kind!r}")
        if not isinstance(row.get("why"), str) or not row["why"].strip():
            raise ManifestError(f"{row_id}: why is required")
        seen.add(row_id)
        validate_row(row)
    return rows


def sha256(path: Path) -> str:
    """Hash a file without loading it all into memory."""
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()
