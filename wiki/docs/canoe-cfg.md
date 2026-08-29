# `canoe.cfg` — the boot-root contract

`canoe.cfg` is the BDS menu state for 7.x. The BDS only reads this file; the
host Python transaction and the device-side installer are the writers. The
file lives under `persist/efisp`, while the raw `efisp` partition contains only
`BDS.efi`.

## Location and syntax

| View | Path |
| --- | --- |
| Android or recovery | `/mnt/vendor/persist/efisp/canoe.cfg` or `/persist/efisp/canoe.cfg` |
| BDS on the `persist` volume | `\efisp\canoe.cfg` |

Every `image` path is relative to the boot root. The file uses 7-bit printable
ASCII, `LF` or `CRLF`, and at most 8192 bytes. At most 24 entries are accepted.
Leading whitespace is ignored, `#` starts a comment, blank lines are ignored,
and an over-long line is skipped rather than truncated.

A line consists of a key, one run of spaces or tabs, and a value extending to
end of line. `entry <id>` opens an entry block. IDs contain 1–31 characters from
`[A-Za-z0-9._-]`; titles contain 1–47 printable ASCII characters. An `image`
path is required, may be at most 198 characters, and cannot contain `.` or `..`
components, doubled separators, or a trailing separator. `/` is folded to `\`.

## Global keys

Global keys must appear before the first `entry`:

| Key | Values | Default | Meaning |
| --- | --- | --- | --- |
| `version` | `1` | required | Configuration format version |
| `generation` | `0..4294967295` | `0` | Monotonic installed-generation number |
| `timeout` | `0..60` | `5` | Seconds before the configured default launches |
| `default` | an entry ID | none | Entry launched without menu input |
| `mode` | `0`, `1`, `2` | `1` | Fallback mode for entries without their own mode |
| `devinfo-repair` | `asneeded`, `never` | `asneeded` | Whether a managed launch may repair `DeviceInfo` |

Inside an entry block, `title`, `image`, `options`, `mode`, and `role` are
valid. A per-entry `mode` overrides the global fallback. File-global keys
appearing in an entry are rejected rather than retroactively applied.

`options` is the command line handed to the image as UEFI LoadOptions. It is
at most 383 characters and is passed through byte for byte: unlike `image` it
is not a path, so `/` is never folded to `\` and a value that looks like a
path is left exactly as written. An empty `options` is a rejected line rather
than a silent no-op.

This is what lets a row hold a payload the BDS does not itself parse. The
image is one of the loaders shipped in the boot root's `tools` directory, and
the payload it should boot is named in `options`:

```text
entry mu
  title Mu-Silicium
  image tools/FdLoader.efi
  options \mu\SM8850.fd 0x9FC00000 0x00300000

entry android-usb
  title Android from images
  image tools/AbootLoader.efi
  options --boot \img\boot.img --vendor-boot \img\vendor_boot.img
```

`FdLoader.efi` takes an FD image path, a hexadecimal load base and a
hexadecimal window size, and chainloads a raw Mu-Silicium or Project-Aloha
firmware descriptor. `AbootLoader.efi` takes `--boot` and `--vendor-boot`,
optionally `--init-boot`, `--dtb-index` and `--cmdline`, and boots an Android
boot image with header version 3 or 4. Both paths are relative to the volume
the loader itself was launched from.

## The two managed rows

Every install writes exactly the following managed rows and no other managed
rows:

| Row | ID | Title | Image | Role | Written when |
| --- | --- | --- | --- | --- | --- |
| Active | `android-a` or `android-b` | `Android (slot A)` or `Android (slot B)` | `boot.efi` | `active` | Every install; always with `default` |
| Backup | `android-backup` | `Android (previous)` | `boot_backup.efi` | `backup` | While `boot_backup.efi` is non-empty; removed when it is not |

Hand-added rows are preserved verbatim. A row whose image is absent is not
invented or compacted by the writer; the BDS simply cannot launch it until the
image exists. The explicit migration for old `boot_a.efi` and `boot_b.efi` rows
is the only exception: those loaders, their sidecars, and their corresponding
`android-a`/`android-b` rows are removed and the migration is reported.

The active ID and title record the GPT slot that the installer labels as
active. The `active` role is functional, not presentation-only. If it disagrees
with the GPT active slot, BDS marks the entry `SlotMismatch`; when that row is
the configured default, BDS withholds unattended launch and forces the menu.
Re-run the installation with the correct slot to repair the label.

`role backup` identifies the previous generation and is also functional menu
metadata. The backup row is managed because its image is `boot_backup.efi`, a
path understood by the BDS. A row naming `boot_a.efi` or `boot_b.efi` is not a
managed slot row and its configured mode has no effect.

## A/B generation lifecycle

On the first install, the selected slot becomes the active row and
`boot.efi` is the only generation. On an update, the live triplet is first
moved to `boot_backup.efi`, with a sidecar removed when its matching source was
absent; the new triplet becomes `boot.efi`. The backup row therefore carries the
previous slot's loader and the previous generation together.

After an OTA, press the module's **Flash To Other Slot** action before rebooting.
It derives the loader for the slot that will boot next and labels the new active
row with that slot. If the action is skipped, the new slot has no managed loader
and boots stock; no configuration row can make a stock ABL load BDS.

## Sidecars and modes

Only the managed paths `boot.efi` and `boot_backup.efi` have sidecars read by
BDS. For those paths, `.gm2p` is the 120-byte KeyMint profile and `.tzmap` is the
256-byte TrustZone interface map belonging to that exact loader. Per-image
sidecars are not honoured for every row: hand-added or `boot_a.efi`/`boot_b.efi`
rows are passthrough rows, so their sidecars are never read.

Mode 0 is a hook-free passthrough. Mode 1 projects the locked DeviceInfo view
and enables the normal managed hooks. Mode 2 additionally uses the matching
profile. A menu mode is a session-only override for the next launch; it is never
written to this file. A per-entry mode applies to that entry, with global
`mode` as the fallback.

A successful Mode 2 derivation means only that `vbmeta` parsed and carries a
signature and public-key blob. It does not prove that the key is the OEM's; no
tool here can prove that. The only automatic protection is detecting whether
the public-key digest changed since the last installed generation. A changed
signer is expected when moving to or from a custom ROM and requires the
operator's explicit allowance for that supplied firmware.

## DeviceInfo repair

A Mode 1 or Mode 2 launch may repair `DeviceInfo` when the observed state does
not satisfy the requested mode. `devinfo-repair asneeded` permits that repair;
`devinfo-repair never` refuses it and continues honestly in Mode 0. Mode 0
neither reads nor writes `DeviceInfo`. The boot log records the observation and
the chosen action before the decision.

## Example

```text
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

entry android-backup
  title Android (previous)
  image boot_backup.efi
  mode 0
  role backup
```

## Empty or invalid configuration

No file, an invalid `version`, or a file with no usable entry is not itself an
error. BDS probes the known managed paths `boot.efi` and `boot_backup.efi` and
shows the menu rather than launching unattended when configuration is missing.
An empty boot root with neither `canoe.cfg` nor `boot.efi` is first run and
enters Super Fastboot so the operator can install the chain.
