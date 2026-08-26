"""Read and atomically rewrite the declarative canoe boot menu."""

from __future__ import annotations

import os
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Final, Literal

MAX_BYTES: Final = 8192
MAX_ENTRIES: Final = 24
MAX_GENERATION: Final = 0xFFFFFFFF
MAX_TIMEOUT: Final = 60
MAX_TITLE_CHARS: Final = 47
MAX_PATH_CHARS: Final = 198
_ID_RE: Final = re.compile(r"^[A-Za-z0-9._-]{1,31}$")
Role = Literal["active", "inactive", "backup", "other"]
DeviceInfoRepair = Literal["asneeded", "never"]


@dataclass(frozen=True, slots=True)
class ConfigError(ValueError):
    """A canoe.cfg value cannot be represented by the BDS parser."""

    message: str

    def __str__(self) -> str:
        return self.message


@dataclass(frozen=True, slots=True)
class ConfigEntry:
    """One installed boot image and its launch policy."""

    id: str
    title: str
    image: str
    mode: int
    role: Role = "other"

    def __post_init__(self) -> None:
        if _ID_RE.fullmatch(self.id) is None:
            raise ConfigError(f"invalid entry id: {self.id!r}")
        if not 0 < len(self.title) <= MAX_TITLE_CHARS or not _printable(self.title):
            raise ConfigError(f"entry title must be 1..{MAX_TITLE_CHARS} ASCII characters")
        if not 0 <= self.mode <= 2:
            raise ConfigError("entry mode must be 0, 1 or 2")
        _role(self.role)
        _validate_image(self.image)


@dataclass(frozen=True, slots=True)
class Config:
    """The complete host-authored canoe.cfg document."""

    entries: tuple[ConfigEntry, ...]
    generation: int = 0
    timeout: int = 5
    default: str | None = None
    mode: int = 1
    devinfo_repair: DeviceInfoRepair = "asneeded"

    def __post_init__(self) -> None:
        if not self.entries or len(self.entries) > MAX_ENTRIES:
            raise ConfigError(f"config must contain 1..{MAX_ENTRIES} entries")
        if not 0 <= self.generation <= MAX_GENERATION:
            raise ConfigError("generation must be in 0..4294967295")
        if not 0 <= self.timeout <= MAX_TIMEOUT:
            raise ConfigError(f"timeout must be in 0..{MAX_TIMEOUT}")
        if not 0 <= self.mode <= 2:
            raise ConfigError("mode must be 0, 1 or 2")
        if self.devinfo_repair not in ("asneeded", "never"):
            raise ConfigError(f"invalid devinfo-repair: {self.devinfo_repair!r}")
        ids = tuple(entry.id for entry in self.entries)
        if len(set(ids)) != len(ids):
            raise ConfigError("entry ids must be unique")
        if self.default is not None and self.default not in ids:
            raise ConfigError(f"default entry does not exist: {self.default}")


def _printable(value: str) -> bool:
    return all(0x20 <= ord(character) <= 0x7E for character in value)


def _role(value: str) -> Role:
    if value in ("active", "inactive", "backup", "other"):
        return value
    raise ConfigError(f"invalid entry role: {value!r}")


def _validate_image(image: str) -> None:
    if not 0 < len(image) <= MAX_PATH_CHARS or not _printable(image):
        raise ConfigError(f"image path must be 1..{MAX_PATH_CHARS} ASCII characters")
    path = image.replace("\\", "/")
    if path.startswith("/"):
        path = path[1:]
    parts = path.split("/")
    if not path or any(part in ("", ".", "..") for part in parts):
        raise ConfigError(f"invalid boot-root-relative image path: {image!r}")


def _image_value(value: str) -> str:
    path = value.replace("\\", "/")
    if path.startswith("/"):
        path = path[1:]
    _validate_image(path)
    return path


def _split(line: str) -> tuple[str, str]:
    for index, character in enumerate(line):
        if character in (" ", "\t"):
            return line[:index], line[index:].lstrip(" \t").rstrip(" \t")
    return line, ""


def _number(value: str, name: str, maximum: int) -> int:
    if not value.isdecimal():
        raise ConfigError(f"{name} must be decimal")
    parsed = int(value)
    if parsed > maximum:
        raise ConfigError(f"{name} must be in 0..{maximum}")
    return parsed


def read_config(path: Path) -> Config:
    """Parse up to the BDS's 8192-byte canoe.cfg boundary."""
    try:
        raw = path.read_bytes()[:MAX_BYTES]
    except OSError as exc:
        raise ConfigError(f"could not read {path}: {exc}") from exc
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError as exc:
        raise ConfigError("canoe.cfg must contain 7-bit ASCII") from exc
    for character in text:
        if character not in ("\r", "\n", "\t") and not 0x20 <= ord(character) <= 0x7E:
            raise ConfigError("canoe.cfg contains a non-printable byte")

    version_seen = False
    generation, timeout, mode = 0, 5, 1
    devinfo_repair: DeviceInfoRepair = "asneeded"
    default: str | None = None
    blocks: list[dict[str, str]] = []
    current: dict[str, str] | None = None
    for raw_line in text.splitlines():
        line = raw_line.strip(" \t\r")
        if not line or line.startswith("#"):
            continue
        key, value = _split(line)
        if key == "version":
            if version_seen or value != "1":
                raise ConfigError("canoe.cfg requires exactly version 1")
            version_seen = True
        elif not version_seen:
            raise ConfigError("canoe.cfg must begin with version 1")
        elif key == "entry":
            if _ID_RE.fullmatch(value) is None:
                raise ConfigError(f"invalid entry id: {value!r}")
            if any(block["id"] == value for block in blocks):
                raise ConfigError(f"duplicate entry id: {value}")
            current = {"id": value}
            blocks.append(current)
        elif current is not None:
            if key in ("title", "image", "role", "mode"):
                current[key] = value
        elif key == "generation":
            generation = _number(value, "generation", MAX_GENERATION)
        elif key == "timeout":
            timeout = _number(value, "timeout", MAX_TIMEOUT)
        elif key == "mode":
            mode = _number(value, "mode", 2)
        elif key == "devinfo-repair":
            match value:
                case "asneeded" | "never":
                    devinfo_repair = value
                case _:
                    raise ConfigError(f"invalid devinfo-repair: {value!r}")
        elif key == "default":
            default = value

    if not version_seen or not blocks:
        raise ConfigError("canoe.cfg has no usable entry")
    entries: list[ConfigEntry] = []
    for block in blocks:
        image = block.get("image")
        if image is None:
            continue
        entry_mode = _number(block.get("mode", str(mode)), "entry mode", 2)
        entries.append(
            ConfigEntry(
                block["id"],
                block.get("title", block["id"]),
                _image_value(image),
                entry_mode,
                _role(block.get("role", "other")),
            )
        )
    if not entries:
        raise ConfigError("canoe.cfg has no usable entry")
    resolved_default = default if default in {entry.id for entry in entries} else None
    return Config(tuple(entries), generation, timeout, resolved_default, mode, devinfo_repair)


def serialize_config(config: Config, *, generation: int | None = None) -> str:
    """Render a complete config, bumping its generation unless overridden."""
    next_generation = config.generation + 1 if generation is None else generation
    if next_generation > MAX_GENERATION:
        raise ConfigError("generation cannot be bumped past 4294967295")
    lines = ["version 1", f"generation {next_generation}", f"timeout {config.timeout}"]
    if config.default is not None:
        lines.append(f"default {config.default}")
    lines.extend((f"mode {config.mode}", f"devinfo-repair {config.devinfo_repair}", ""))
    for entry in config.entries:
        lines.extend(
            (
                f"entry {entry.id}",
                f"  title {entry.title}",
                f"  image {entry.image}",
                f"  mode {entry.mode}",
                f"  role {entry.role}",
                "",
            )
        )
    encoded = "\n".join(lines).encode("ascii")
    if len(encoded) > MAX_BYTES:
        raise ConfigError("canoe.cfg exceeds 8192 bytes")
    return encoded.decode("ascii")


def write_config(path: Path, config: Config) -> int:
    """Atomically replace path with the generated config and return its generation."""
    generation = config.generation + 1
    text = serialize_config(config, generation=generation)
    temporary = path.with_name(f".{path.name}.tmp")
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary.write_text(text, encoding="ascii", newline="\n")
        os.replace(temporary, path)
    except OSError as exc:
        temporary.unlink(missing_ok=True)
        raise ConfigError(f"could not write {path}: {exc}") from exc
    return generation
