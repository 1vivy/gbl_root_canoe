"""One canoe.cfg operation, exactly as `canoe_boot_entry.sh` applies it.

The device runs the shell writer and the host runs this module, so the two must
agree byte for byte. That is why the operations are replayed one at a time
against a mutable document instead of rendering a desired end state: the shell
writer bumps `generation` once per invocation, and an install that upserts two
rows therefore lands a generation two higher than one that upserts one. A
"render the final state" implementation would be off by the number of rows.

Rendering itself is delegated to `config.serialize_config`, which is already the
canonical serializer; nothing here formats a line.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Final

from .config import (
    MAX_ENTRIES,
    MAX_GENERATION,
    Config,
    ConfigEntry,
    ConfigError,
    DeviceInfoRepair,
    Role,
    read_config,
    serialize_config,
)
from .errors import CanoeError

ACTIVE_IDS: Final = {"_a": "android-a", "_b": "android-b"}
ACTIVE_TITLES: Final = {"_a": "Android (slot A)", "_b": "Android (slot B)"}
BACKUP_ID: Final = "android-backup"
BACKUP_TITLE: Final = "Android (previous)"
CONFIG_NAME: Final = "canoe.cfg"


@dataclass(slots=True)
class Document:
    """canoe.cfg between two operations.

    Mutation is the documented purpose: the shell writer's semantics are
    defined as a sequence of in-place upserts and removals, and replaying that
    sequence is what keeps the two implementations byte-identical.
    """

    entries: list[ConfigEntry] = field(default_factory=list)
    generation: int = 0
    timeout: int = 5
    default: str | None = None
    mode: int = 1
    devinfo_repair: DeviceInfoRepair = "asneeded"


@dataclass(frozen=True, slots=True)
class Row:
    """The arguments of one `canoe_boot_entry.sh set`.

    `mode` unset keeps whatever mode the entry already carried, and a brand new
    entry inherits the file-global fallback -- the property that lets a caller
    upsert a row without deciding policy for it. `global_mode` unset leaves the
    file-global fallback alone.
    """

    id: str
    title: str
    image: str
    role: Role
    mode: int | None = None
    global_mode: int | None = None
    default: bool = False


def load(path: Path) -> Document:
    """Read an installed canoe.cfg, or start the empty document a first install writes."""
    if not path.exists():
        return Document()
    try:
        config = read_config(path)
    except ConfigError as exc:
        raise CanoeError(f"{path}: {exc}") from exc
    return Document(
        list(config.entries),
        config.generation,
        config.timeout,
        config.default,
        config.mode,
        config.devinfo_repair,
    )


def holds(document: Document, entry_id: str) -> bool:
    """Whether `entry_id` is one of the document's rows."""
    return any(entry.id == entry_id for entry in document.entries)


def upsert(document: Document, row: Row) -> int:
    """Create or replace one row in place, returning the generation it produced."""
    if row.global_mode is not None:
        document.mode = row.global_mode
    index = _index_of(document, row.id)
    entry = ConfigEntry(row.id, row.title, row.image, _mode_for(document, row, index), row.role)
    if index is None:
        if len(document.entries) >= MAX_ENTRIES:
            raise CanoeError(f"canoe.cfg already holds {MAX_ENTRIES} entries")
        document.entries.append(entry)
    else:
        document.entries[index] = entry
    if row.default:
        document.default = row.id
    return _advance(document)


def remove(document: Document, entry_id: str) -> int:
    """Drop one row, returning the generation it produced."""
    index = _index_of(document, entry_id)
    if index is None:
        raise CanoeError(f"no such entry: {entry_id}")
    del document.entries[index]
    if not document.entries:
        raise CanoeError("canoe.cfg would have no usable entry")
    return _advance(document)


def render(document: Document) -> str:
    """Serialize the document through the one canonical serializer."""
    try:
        config = Config(
            tuple(document.entries),
            document.generation,
            document.timeout,
            document.default,
            document.mode,
            document.devinfo_repair,
        )
        return serialize_config(config, generation=document.generation)
    except ConfigError as exc:
        raise CanoeError(f"canoe.cfg cannot be written: {exc}") from exc


def _index_of(document: Document, entry_id: str) -> int | None:
    for index, entry in enumerate(document.entries):
        if entry.id == entry_id:
            return index
    return None


def _mode_for(document: Document, row: Row, index: int | None) -> int:
    if row.mode is not None:
        return row.mode
    return document.mode if index is None else document.entries[index].mode


def _advance(document: Document) -> int:
    """Re-point a departed default and bump the generation, as the writer does."""
    document.default = _resolve_default(document)
    if document.generation >= MAX_GENERATION:
        raise CanoeError(f"generation cannot be bumped past {MAX_GENERATION}")
    document.generation += 1
    return document.generation


def _resolve_default(document: Document) -> str:
    """A default naming a departed row is worse than none: prefer active, then the first."""
    current = document.default
    if current is not None and holds(document, current):
        return current
    for entry in document.entries:
        if entry.role == "active":
            return entry.id
    return document.entries[0].id
