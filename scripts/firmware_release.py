#!/usr/bin/env python3
"""Package one firmware build, verify its identities, and prepare a draft only."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TOOLS = (
    "ArbTools.efi", "BLTools.efi", "CrashTools.efi", "LogTools.efi",
    "MdTools.efi", "RebootTools.efi", "SurfaceTools.efi", "UsbTools.efi",
)
EFI_FILES = ("BDS.efi", *TOOLS)
VERSION = re.compile(r"\d+\.\d+\.\d+(?:-[A-Za-z0-9]+(?:[.-][A-Za-z0-9]+)*)?")
SHA256 = re.compile(r"[0-9a-f]{64}")


def command(args: list[str], root: Path = ROOT) -> str:
    result = subprocess.run(args, cwd=root, text=True, capture_output=True, check=False)
    if result.returncode:
        raise ValueError(f"{args[0]} failed: {result.stderr.strip()}")
    return result.stdout.strip()


def canonical_version(text: str) -> str:
    matches = re.findall(r"^CANOE_VERSION\s*=\s*(\S+)\s*$", text, re.MULTILINE)
    if len(matches) != 1 or not VERSION.fullmatch(matches[0]):
        raise ValueError("version.mk must contain one canonical CANOE_VERSION")
    return matches[0]


def resolve_tag(tag: str, root: Path = ROOT) -> tuple[str, str]:
    if not tag.startswith("release-") or not VERSION.fullmatch(tag[8:]):
        raise ValueError("Use an existing release-<version> tag")
    commit = command(["git", "rev-parse", "--verify", f"refs/tags/{tag}^{{commit}}"], root)
    version = canonical_version(command(["git", "show", f"{commit}:version.mk"], root))
    if tag != f"release-{version}":
        raise ValueError("The release tag must match version.mk at that exact commit")
    return version, commit


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def inspect_efi(data: bytes, name: str) -> None:
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError(f"{name} is not a PE EFI image")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if pe + 94 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError(f"{name} has an invalid PE header")
    machine, optional_size = struct.unpack_from("<H", data, pe + 4)[0], struct.unpack_from("<H", data, pe + 20)[0]
    if machine != 0xAA64 or optional_size < 70 or pe + 24 + optional_size > len(data):
        raise ValueError(f"{name} must be an ARM64 EFI application")
    if struct.unpack_from("<H", data, pe + 24)[0] != 0x20B or struct.unpack_from("<H", data, pe + 24 + 68)[0] != 10:
        raise ValueError(f"{name} must be a PE32+ EFI application")


def identity(data: bytes) -> dict:
    return {"bytes": len(data), "sha256": digest(data)}


def make_manifest(version: str, source: str, artifacts: dict[str, bytes]) -> dict:
    if not VERSION.fullmatch(version) or not re.fullmatch(r"[0-9a-f]{40}", source):
        raise ValueError("Invalid firmware version or source commit")
    if set(artifacts) != set(EFI_FILES):
        raise ValueError("The firmware build must contain BDS and all eight standalone tools")
    for name, data in artifacts.items():
        inspect_efi(data, name)
    return {
        "schemaVersion": 1, "product": "canoe-bds", "version": version,
        "tag": f"release-{version}", "source": source, "dirty": False,
        **identity(artifacts["BDS.efi"]),
        "tools": [{"name": name, **identity(artifacts[name])} for name in TOOLS],
    }


def package(root: Path = ROOT) -> Path:
    if command(["git", "status", "--porcelain", "--untracked-files=normal"], root):
        raise ValueError("Firmware packaging requires a clean source checkout")
    version = canonical_version((root / "version.mk").read_text())
    source = command(["git", "rev-parse", "HEAD"], root)
    build = root / "submodules/uefi/build"
    artifacts = {}
    for name in EFI_FILES:
        candidate = build / name
        if candidate.is_symlink() or not candidate.is_file():
            raise ValueError(f"Firmware build is missing an ordinary {name} file")
        artifacts[name] = candidate.read_bytes()
    manifest = make_manifest(version, source, artifacts)
    # This is a dedicated generated directory, never an arbitrary caller path.
    output = root / ".work/firmware-release"
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.is_symlink():
        raise ValueError("Firmware output must not be a symlink")
    with tempfile.TemporaryDirectory(prefix="firmware-stage-", dir=output.parent) as temporary:
        staging = Path(temporary)
        for name, data in artifacts.items():
            (staging / name).write_bytes(data)
        (staging / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        sums = [f"{digest(item.read_bytes())}  {item.name}" for item in sorted(staging.iterdir())]
        (staging / "SHA256SUMS").write_text("\n".join(sums) + "\n")
        verify(staging)
        if output.exists():
            shutil.rmtree(output)
        shutil.copytree(staging, output)
    return output


def verify(directory: Path) -> dict:
    expected_names = set(EFI_FILES) | {"manifest.json", "SHA256SUMS"}
    if {item.name for item in directory.iterdir()} != expected_names:
        raise ValueError("Firmware release contains missing or unexpected files")
    for item in directory.iterdir():
        if item.is_symlink() or not item.is_file():
            raise ValueError("Firmware release must contain ordinary files")
    raw = (directory / "manifest.json").read_bytes()
    manifest = json.loads(raw)
    if manifest.get("schemaVersion") != 1 or manifest.get("product") != "canoe-bds" or manifest.get("dirty") is not False:
        raise ValueError("Firmware release requires a clean, versioned manifest")
    artifacts = {name: (directory / name).read_bytes() for name in EFI_FILES}
    if manifest != make_manifest(manifest.get("version", ""), manifest.get("source", ""), artifacts):
        raise ValueError("Firmware bytes or metadata differ from their manifest")
    expected = {name: digest(data) for name, data in artifacts.items()}
    expected["manifest.json"] = digest(raw)
    actual = {}
    for line in (directory / "SHA256SUMS").read_text().splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9][A-Za-z0-9._-]*)", line)
        if not match or match[2] in actual:
            raise ValueError("Invalid or repeated firmware checksum entry")
        actual[match[2]] = match[1]
    if actual != expected:
        raise ValueError("Firmware checksum list differs from the exact release files")
    return manifest


def draft(tag: str, directory: Path, root: Path = ROOT) -> None:
    version, commit = resolve_tag(tag, root)
    manifest = verify(directory)
    if manifest["version"] != version or manifest["source"] != commit or manifest["tag"] != tag:
        raise ValueError("Firmware release bytes do not belong to the selected tag")
    if command(["git", "rev-parse", "HEAD"], root) != commit:
        raise ValueError("Check out the exact release commit before uploading firmware")
    repo = os.environ.get("GITHUB_REPOSITORY") or command(["gh", "repo", "view", "--json", "nameWithOwner", "--jq", ".nameWithOwner"], root)
    if not re.fullmatch(r"[\w.-]+/[\w.-]+", repo):
        raise ValueError("Invalid GitHub repository")
    pages = json.loads(command(["gh", "api", "--paginate", "--slurp", f"repos/{repo}/releases"], root))
    releases = [item for page in pages for item in page]
    existing = next((item for item in releases if item["tag_name"] == tag), None)
    if existing and not existing["draft"]:
        raise ValueError("Published firmware releases are not replaced; use a new version")
    if not existing:
        with tempfile.TemporaryDirectory(prefix="canoe-firmware-notes-") as temporary:
            notes = Path(temporary) / "notes.md"
            notes.write_text(f"CANOE-BDS {version}\n\nSource: {commit}\n\nRaw ARM64 BDS.efi and eight standalone EFI tools are attached. manifest.json records each exact length and SHA-256; SHA256SUMS also covers the manifest.\n\nThis draft contains a new firmware build. Qualify these exact bytes before publishing or updating the manager's firmware pin. Rebuilding the same sources need not reproduce an earlier binary.\n")
            args = ["gh", "release", "create", tag, "--repo", repo, "--verify-tag", "--draft", "--title", f"CANOE-BDS {version}", "--notes-file", str(notes)]
            if "-" in version:
                args.append("--prerelease")
            command(args, root)
    names = [*EFI_FILES, "manifest.json", "SHA256SUMS"]
    command(["gh", "release", "upload", tag, "--repo", repo, "--clobber", *[str(directory / name) for name in names]], root)
    print(f"Draft {tag} contains the verified firmware build. It has not been published.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("package")
    tag = sub.add_parser("tag"); tag.add_argument("tag")
    check = sub.add_parser("verify"); check.add_argument("directory", type=Path)
    upload = sub.add_parser("draft"); upload.add_argument("tag"); upload.add_argument("directory", type=Path)
    args = parser.parse_args()
    try:
        if args.action == "package":
            print(package())
        elif args.action == "verify":
            result = verify(args.directory)
            print(f"Verified CANOE-BDS {result['version']} from {result['source']}")
        elif args.action == "tag":
            _, commit = resolve_tag(args.tag)
            if os.environ.get("GITHUB_OUTPUT"):
                with open(os.environ["GITHUB_OUTPUT"], "a") as output:
                    output.write(f"commit={commit}\ntag={args.tag}\n")
            print(f"{args.tag}: {commit}")
        elif args.action == "draft":
            draft(args.tag, args.directory.resolve())
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.exit(1, f"firmware release: {error}\n")


if __name__ == "__main__":
    main()
