#!/usr/bin/env python3
"""Package one firmware build, verify its identities, and prepare a draft only."""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TOOLS = (
    "ArbTools.efi", "BLTools.efi", "CrashTools.efi", "EudTools.efi",
    "LogTools.efi", "MdTools.efi", "RebootTools.efi", "SurfaceTools.efi",
    "UsbTools.efi",
)
EFI_FILES = ("BDS.efi", *TOOLS)
# The Android one-shot installer ships in the same release as the firmware it
# writes, so the archive cannot be paired with a different BDS. Its contents
# are a contract rather than whatever a package happened to build: the commands
# install-canoe.sh invokes by name, the reviewed tool set the installers stage
# into the boot root, and the firmware images themselves.
ONE_SHOT_COMMANDS = ("canoe-image", "canoe-provision", "canoe-bootmgr", "mode2_profile")
ONE_SHOT_TOOLS = ("ArbTools.efi", "BLTools.efi", "RebootTools.efi", "SurfaceTools.efi", "UsbTools.efi")
ONE_SHOT_FILES = ("BDS.efi", "README.txt", "install-canoe.sh")
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


def one_shot_name(version: str) -> str:
    return f"canoe-one-shot-{version}-android-arm64.zip"


def inspect_one_shot(data: bytes, version: str, artifacts: dict[str, bytes]) -> dict:
    """Validate the Android one-shot package and return its release identity."""
    if not VERSION.fullmatch(version):
        raise ValueError("Invalid firmware version")
    files = {*ONE_SHOT_FILES, *(f"bin/{name}" for name in ONE_SHOT_COMMANDS),
             *(f"efisp/tools/{name}" for name in ONE_SHOT_TOOLS)}
    directories = {"bin/", "efisp/", "efisp/tools/"}
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as package:
            members = package.infolist()
            names = [item.filename for item in members]
            if len(set(names)) != len(names):
                raise ValueError("The one-shot package repeats an archive member")
            if {name for name in names if not name.endswith("/")} != files or \
                    {name for name in names if name.endswith("/")} - directories:
                raise ValueError("The one-shot package must contain exactly the installer, its commands, "
                                 "the staged EFI tools, BDS and the readme")
            for item in members:
                mode = item.external_attr >> 16
                if item.filename.endswith("/"):
                    if not stat.S_ISDIR(mode):
                        raise ValueError(f"The one-shot package member {item.filename} is not a directory")
                    continue
                wanted = 0o755 if item.filename == "install-canoe.sh" or item.filename.startswith("bin/") else 0o644
                if not stat.S_ISREG(mode) or stat.S_IMODE(mode) != wanted:
                    raise ValueError(f"The one-shot package member {item.filename} must be an ordinary file "
                                     f"with mode {wanted:o}")
            payloads = {name: package.read(name) for name in names if not name.endswith("/")}
    except zipfile.BadZipFile as error:
        raise ValueError(f"The one-shot package is not a readable zip: {error}") from error
    for name in ("BDS.efi", *ONE_SHOT_TOOLS):
        member = name if name == "BDS.efi" else f"efisp/tools/{name}"
        if payloads[member] != artifacts[name]:
            raise ValueError(f"The one-shot package carries a different {name} than this firmware build")
    for name in ONE_SHOT_COMMANDS:
        command = payloads[f"bin/{name}"]
        if len(command) < 64 or command[:4] != b"\x7fELF" or command[4] != 2 or \
                struct.unpack_from("<H", command, 18)[0] != 0xB7:
            raise ValueError(f"The one-shot package command bin/{name} must be an ARM64 Android ELF executable")
    return {"name": one_shot_name(version), **identity(data)}


def make_manifest(version: str, source: str, artifacts: dict[str, bytes], one_shot: dict) -> dict:
    if not VERSION.fullmatch(version) or not re.fullmatch(r"[0-9a-f]{40}", source):
        raise ValueError("Invalid firmware version or source commit")
    if set(artifacts) != set(EFI_FILES):
        raise ValueError("The firmware build must contain BDS and all eight standalone tools")
    if set(one_shot) != {"name", "bytes", "sha256"} or one_shot["name"] != one_shot_name(version) or \
            not isinstance(one_shot["bytes"], int) or one_shot["bytes"] <= 0 or \
            not SHA256.fullmatch(str(one_shot["sha256"])):
        raise ValueError("The manifest must describe the Android one-shot package")
    for name, data in artifacts.items():
        inspect_efi(data, name)
    return {
        "schemaVersion": 1, "product": "canoe-bds", "version": version,
        "tag": f"release-{version}", "source": source, "dirty": False,
        **identity(artifacts["BDS.efi"]),
        "tools": [{"name": name, **identity(artifacts[name])} for name in TOOLS],
        "oneShot": one_shot,
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
    package_file = root / "targets/one_shot_android/build" / one_shot_name(version)
    if package_file.is_symlink() or not package_file.is_file():
        raise ValueError(f"The Android one-shot package is missing: {package_file}; run `make target_one_shot_android`")
    one_shot = package_file.read_bytes()
    manifest = make_manifest(version, source, artifacts, inspect_one_shot(one_shot, version, artifacts))
    # This is a dedicated generated directory, never an arbitrary caller path.
    output = root / ".work/firmware-release"
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.is_symlink():
        raise ValueError("Firmware output must not be a symlink")
    with tempfile.TemporaryDirectory(prefix="firmware-stage-", dir=output.parent) as temporary:
        staging = Path(temporary)
        for name, data in artifacts.items():
            (staging / name).write_bytes(data)
        (staging / manifest["oneShot"]["name"]).write_bytes(one_shot)
        (staging / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        sums = [f"{digest(item.read_bytes())}  {item.name}" for item in sorted(staging.iterdir())]
        (staging / "SHA256SUMS").write_text("\n".join(sums) + "\n")
        verify(staging)
        if output.exists():
            shutil.rmtree(output)
        shutil.copytree(staging, output)
    return output


def verify(directory: Path) -> dict:
    raw = (directory / "manifest.json").read_bytes()
    manifest = json.loads(raw)
    if manifest.get("schemaVersion") != 1 or manifest.get("product") != "canoe-bds" or manifest.get("dirty") is not False:
        raise ValueError("Firmware release requires a clean, versioned manifest")
    version = manifest.get("version", "")
    one_shot = one_shot_name(version) if isinstance(version, str) and VERSION.fullmatch(version) else ""
    expected_names = set(EFI_FILES) | {"manifest.json", "SHA256SUMS"} | ({one_shot} if one_shot else set())
    if {item.name for item in directory.iterdir()} != expected_names:
        raise ValueError("Firmware release contains missing or unexpected files")
    for item in directory.iterdir():
        if item.is_symlink() or not item.is_file():
            raise ValueError("Firmware release must contain ordinary files")
    artifacts = {name: (directory / name).read_bytes() for name in EFI_FILES}
    identity = inspect_one_shot((directory / one_shot).read_bytes(), version, artifacts)
    if manifest != make_manifest(version, manifest.get("source", ""), artifacts, identity):
        raise ValueError("Firmware bytes or metadata differ from their manifest")
    expected = {name: digest(data) for name, data in artifacts.items()}
    expected["manifest.json"] = digest(raw)
    expected[identity["name"]] = identity["sha256"]
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
            notes.write_text(f"CANOE-BDS {version}\n\nSource: {commit}\n\nRaw ARM64 BDS.efi and eight standalone EFI tools are attached, together with the Android one-shot installer package that carries this exact BDS and the reviewed EFI tool set. manifest.json records each exact length and SHA-256; SHA256SUMS also covers the manifest.\n\nThis draft contains a new firmware build. Qualify these exact bytes before publishing or updating the manager's firmware pin. Rebuilding the same sources need not reproduce an earlier binary.\n")
            args = ["gh", "release", "create", tag, "--repo", repo, "--verify-tag", "--draft", "--title", f"CANOE-BDS {version}", "--notes-file", str(notes)]
            if "-" in version:
                args.append("--prerelease")
            command(args, root)
    names = [*EFI_FILES, "manifest.json", "SHA256SUMS", manifest["oneShot"]["name"]]
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
