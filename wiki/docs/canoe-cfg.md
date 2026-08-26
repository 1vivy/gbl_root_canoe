# `canoe.cfg` — the boot root contract

`canoe.cfg` is the only source of menu state in 7.x. It replaces the three
1 KiB records that 6.x kept in the tail of the `efisp` partition.

## Why the records moved

The `efisp` partition holds one thing and nothing else: `BDS.efi`, written raw.
Anything past the image was scratch that 6.x borrowed for a preferred-mode
record at `end - 3072`, a default-entry record at `end - 2048` and a custom-entry
record at `end - 1024`. Two problems followed from that:

- A `dd` of a new `BDS.efi` writes only the image length, so a stale record
  outlived the loader that wrote it. A mode selected under one build silently
  applied under the next.
- The BDS had to be a writer. Every write path is a way to brick a device that
  is already one failed boot away from EDL.

7.x inverts it. **The BDS never writes to storage.** It reads `canoe.cfg` and
renders it. There is exactly one writer:
`tools/canoe-device/canoe_boot_entry.sh`. The host toolkit, the KernelSU module,
and the OTA watcher all call this same script.

The writer's interface is:

```text
sh canoe_boot_entry.sh set <boot_root> --id ID --title TITLE --image IMAGE \
  --role active|inactive|backup|other [--mode 0|1|2] [--default] \
  [--global-mode 0|1|2] [--timeout SECONDS] \
  [--devinfo-repair asneeded|never]
sh canoe_boot_entry.sh remove <boot_root> --id ID
sh canoe_boot_entry.sh show <boot_root>
```

`set` is an UPSERT: it creates or replaces the named entry in place while
preserving every other entry verbatim, including hand-added custom-ROM entries
and OTA-added slot entries. `show` writes nothing. This page remains the
normative grammar and path-limit reference for the document the writer emits.
The host and device-side callers have real read-write access to `persist`; the
BDS does not, and this is not an oversight to be engineered around:

- The ext4 driver linked into the BDS is read-only **by construction**. Upstream
  `edk2-platforms` `Features/Ext4Pkg` returns `EFI_WRITE_PROTECTED` from
  `Ext4WriteFile`, `EFI_WARN_DELETE_FAILURE` from `Ext4Delete` and
  `EFI_UNSUPPORTED` from `Ext4SetInfo` ("no write support just yet"). No
  maintained read-write ext4 driver exists in the EDK2 or Project Mu ecosystem.
- The firmware on this platform **rejects EFI variables it does not already
  know about**, so the stock UEFI answer — `Boot####` and `BootOrder` variables
  driven through `UefiBootManagerLib`, which is what Project Mu's own boot menu
  uses — is unavailable here. That is why this file exists at all rather than
  the menu simply adopting the standard abstraction.

6.x reacted to those two facts by writing raw bytes past the end of a partition
image. 7.x reacts by not writing.

## Location

| Seen from | Path |
| --- | --- |
| Android / recovery | `/persist/efisp/canoe.cfg` (or `/mnt/vendor/persist/efisp/canoe.cfg`) |
| The BDS, on the ext4 `persist` volume | `\efisp\canoe.cfg` |

`\efisp` is the boot root: every `image` path in the file is resolved relative
to it.

## Encoding and limits

- 7-bit ASCII. Bytes outside `0x20..0x7e` (other than `\r` and `\n`) reject the
  line.
- `LF` or `CRLF`.
- At most 8192 bytes are read; the rest is ignored.
- At most 24 entries; later ones are dropped with a log marker.

The lexer strips leading whitespace, treats `#` as a comment marker, ignores
blank lines, and skips an over-long line rather than truncating it.

A line is `key` then one run of spaces or tabs then `value`. The value runs to
end of line with trailing whitespace trimmed.

## Global keys

They appear before the first `entry`. Indentation is cosmetic, so after an
`entry` line every key belongs to that entry: `mode` there sets the entry's own
mode, and a key that exists only at file scope — `timeout`, `default`,
`generation`, `devinfo-repair` — is counted as rejected rather than retro-applied.

| Key | Values | Default | Meaning |
| --- | --- | --- | --- |
| `version` | `1` | — | **Required.** Any other value rejects the whole file. |
| `generation` | `0..4294967295` | `0` | The author's monotonic counter. Display and diagnostics only. |
| `timeout` | `0..60` | `5` | Seconds the menu waits before launching `default`. `0` launches at once. |
| `default` | an entry id | none | The entry an unattended boot launches. |
| `mode` | `0`, `1`, `2` | `1` | Fallback for entries that declare no `mode`. |
| `devinfo-repair` | `asneeded`, `never` | `asneeded` | Whether a managed launch may repair `DeviceInfo`. See below. |

## Entry blocks

`entry <id>` opens a block. Indentation is cosmetic.

| Key | Values | Default | Meaning |
| --- | --- | --- | --- |
| `entry` | id: 1–31 chars of `[A-Za-z0-9._-]` | — | Opens the block. A duplicate id rejects the later block. |
| `title` | 1–47 printable ASCII chars | the id | The menu row text. |
| `image` | boot-root-relative path, up to 198 chars | — | **Required.** No `.` or `..` component, no double separator, no trailing separator, and `/` is folded to `\`. |
| `mode` | `0`, `1`, `2` | the global `mode` | The boot policy this image is launched under. |
| `role` | `active`, `inactive`, `backup`, `other` | `other` | Presentation only. The menu suffixes the row. |

An entry whose `image` is missing from the volume is dropped, so a config that
outlives its images degrades to the entries that are still installed rather than
offering rows that cannot boot.

## Mode is per entry

6.x carried one global mode record. That made a mismatch representable: the
`.gm2p` KeyMint profile and the `.tzmap` TrustZone map are **already** per-image
sidecars, derived from one specific ABL and one specific `vbmeta`, so a global
Mode 2 could be applied to an image that had no profile of its own.

In 7.x the mode belongs to the entry that owns those sidecars. The global `mode`
survives only as the fallback for entries that do not care. The menu still
offers a session-only override, which applies to the next launch and is never
written anywhere.

## Roles and the third entry

A device with A/B slots has two ABLs plus the backup the install rotated out.
All three are ordinary entries; `role` is what tells them apart on screen:

```
Android (slot A)          (active)
Android (slot B)          (inactive)
Android (previous)        (backup)
```

The BDS derives no slot state of its own. Which image is active is a fact the
authoring process already knows, and it writes it down.

## DeviceInfo repair policy

A managed launch under Mode 1 or Mode 2 needs the backing `DeviceInfo` to read
unlocked, because the projection is what makes ABL see a locked device while
the real state stays unlocked. 6.x repaired `DeviceInfo` unconditionally, on
every managed launch, in every mode.

7.x gates it:

- Mode 0 is a hook-free passthrough. Nothing is read, nothing is written.
- `devinfo-repair never` refuses the repair outright; a launch that needed it is
  reported and continues honestly in Mode 0.

Either way the **observed** state is recorded before any decision, as
`SFB: MARK devinfo-repair observed-unlocked=<0|1> observed-critical=<0|1>
required=<0|1> action=<none|repair|refused>`.

## Example

```
# canoe.cfg - managed by canoe_boot_entry.sh. Hand edits are fine; the
# authoring tool rewrites the whole file and will drop comments.
version 1
generation 4
timeout 5
default android-a
mode 1
devinfo-repair asneeded

entry android-a
  title Android (slot A)
  image boot.efi
  mode 1
  role active

entry android-b
  title Android (slot B)
  image boot_b.efi
  mode 1
  role inactive

entry android-backup
  title Android (previous)
  image boot_backup.efi
  mode 0
  role backup
```

## Absent or unparseable

No `canoe.cfg`, a bad `version`, or a file with no usable entry is not an error.
The BDS probes the boot root for the known managed names `boot.efi` and then
`boot_backup.efi`, offering them as `Android` and `Android (previous)`, then
adds anything discovered on removable/ESP media. All such entries use the
built-in default mode, and the menu is shown rather than launching unattended.

An **empty boot root** — no `canoe.cfg` and no `boot.efi` — is the first-run
signal. The BDS says so and hands straight to Super Fastboot, which is the
channel the host tool needs to install anything at all.
