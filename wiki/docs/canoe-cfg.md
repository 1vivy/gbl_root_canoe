# `canoe.cfg` — the boot-root contract

`canoe.cfg` is the BDS menu state for 7.x. In 7.0.0-b2 the boot policy is
explicit: fresh installs default to Silent mode, while the writer and BDS
share the grammar below. The BDS only reads this file; the host
`canoe-bootmgr` transaction and the device-side module are the writers. The
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
| `menu-mode` | `silent`, `menu` | `silent` for fresh installs | Startup policy |
| `key-window` | `0..=10000` | `1200` | Volume-key sampling window in milliseconds |
| `menu-timeout` | `0..=300` | `5` | Menu countdown in seconds; only in Menu mode |
| `default` | an entry ID or `bls:<stem>` | none | Row launched without menu input |
| `mode` | `0`, `1`, `2` | `1` | Fallback mode for entries without their own mode |
| `devinfo-repair` | `asneeded`, `never` | `asneeded` | Whether a managed launch may repair `DeviceInfo` |

`key-window` is inclusive at both bounds. `key-window 0` means no sampling:
Silent mode launches the default immediately, while Menu mode still opens the
menu. In Silent mode, Volume Up during the key window opens the menu and then
waits indefinitely for input; Volume Down takes the existing fastboot path; no
key launches the default immediately. In Menu mode, Volume Down during the key
window takes fastboot, then the menu always opens. The menu counts down for
`menu-timeout` seconds and launches the default; any key cancels the countdown
and leaves the menu waiting indefinitely. `menu-timeout 0` disables automatic
launch.

The writer never emits `timeout`. The BDS accepts a pre-b2 `timeout N` line only
as a compatibility alias for `menu-mode menu` plus `menu-timeout N`; it is not a
rejected line and is never written by current tools.

`default` may name any resolvable `canoe.cfg` entry or a discovered BLS Type #1
row as `bls:<stem>`. The stem is the case-folded lowercase `.conf` basename and
must match `^[a-z0-9._-]{1,63}$` (for example, `loader/entries/pmOS.conf`
becomes `bls:pmos`). A BLS default remains passthrough: it has no managed
sidecars, mode hooks, or slot semantics.
USB-hosted BLS rows are not eligible as
unattended defaults. If the configured default cannot be resolved, BDS opens
the menu, shows the existing rejected/notice surface, and waits; it never
falls through to another row.
Inside an entry block, `title`, `image`, `options`, `mode`, and `role` are
valid. A per-entry `mode` overrides the global fallback. File-global keys
appearing in an entry are rejected rather than retroactively applied.

The complete writer grammar is:

```text
version 1
generation N
menu-mode silent|menu
key-window 1200
menu-timeout 5
default android-a          # or: default bls:pmos
mode 0|1|2
devinfo-repair asneeded|never
```

`options` is the command line handed to the image as UEFI LoadOptions. It is
at most 383 characters and is passed through byte for byte: unlike `image` it
is not a path, so `/` is never folded to `\` and a value that looks like a
path is left exactly as written. An empty `options` is a rejected line rather
than a silent no-op.

This is what lets a row hold a payload the BDS does not itself parse. BDS is a
chainloader selector: it starts a PE and hands over `options` byte for byte,
and the image on the other end owns its argument grammar entirely.

```text
entry mu
  title Mu-Silicium
  image mu/place.efi
  options \efisp\mu\Mu-infiniti.bin

entry grub
  title GRUB
  image grub/grubaa64.efi
```

Neither image is shipped by this project. BDS carries no payload loaders — see
[Chainloading a third-party UEFI stack](./chainload.md) for why, and for what a
third-party stack has to ship to be launchable. `place.efi` comes from the
`canoe-uefi-handoff` side project and takes a single path, because the blob it
enters describes its own load base and window size.

### The two path namespaces

`image` and a launched image's own `options` path do not resolve the same way,
and mixing them up is the one mistake that makes a correct entry fail.

`image` is resolved by the BDS, which prepends the volume's boot root. On the
ext4 `persist` partition that boot root is the `\efisp` directory, so
`image mu/place.efi` loads `\efisp\mu\place.efi`. On a FAT volume the boot root
is the volume root, so the same value loads `\mu\place.efi`. FAT of any width
counts: this device ships no FAT32 partition at all, and a stick formatted
FAT16 is an ordinary boot volume.

`options` is handed over untouched, and the launched image opens any path in it
against the raw filesystem root of the volume it was itself loaded from. It
knows nothing about the boot root. A payload staged in `persist/efisp/mu` must
therefore be written `\efisp\mu\...`; the same payload on a FAT stick is
written `\mu\...`.

This was confirmed on hardware: a row whose `options` named the FAT-style
`\mu\Mu-infiniti.fd` reported `Not Found`, while `image mu/…` resolved through
the boot root in the same launch.

Any load base and window size in `options` belong to the payload, not to Canoe.
For a Mu-Silicium build they are the `[uefi_fd]` `base` and `size` from that
device's `Resources/Configs/<codename>.toml`, which match the `UEFI_FD` row of
its `MemoryMapLib.c`; for a Project-Aloha config they are `StackBase` and
`StackSize`. The values above are OnePlus 15 (`infiniti`).

## Managed A/B triplets

The 7.0.0-b2 writer manages one complete triplet per installed slot:

| Slot | ID | Title written by `canoe-bootmgr` | Image | Sidecars |
| --- | --- | --- | --- | --- |
| A | `android-a` | `Android A` | `boot_a.efi` | `boot_a.efi.gm2p`, `boot_a.efi.tzmap` |
| B | `android-b` | `Android B` | `boot_b.efi` | `boot_b.efi.gm2p`, `boot_b.efi.tzmap` |
| Previous generation | `android-backup` | `Android (previous)` | `boot_backup.efi` | `boot_backup.efi.gm2p`, `boot_backup.efi.tzmap` |

`boot_a.efi` and `boot_b.efi` are independent managed loaders. Their `.gm2p`
sidecar is exactly 120 bytes and their `.tzmap` sidecar is exactly 256 bytes;
each sidecar must belong to the loader beside it. The backup triplet has the
same sidecar sizes.

The writer emits `android-a` and `android-b` only for slots that have a valid
installed triplet. It never creates an empty placeholder row for the other
slot. The installed active slot is marked `role active`; another installed
slot is `role inactive`. `android-backup` exists only while
`boot_backup.efi` and both matching sidecars form a valid previous generation.
The installer refreshes managed rows and does not invent a `default`; set a
desired default explicitly with the boot-manager default command.

Hand-added rows are preserved verbatim. A row whose image is absent is not
invented or compacted by the writer; BDS simply skips it until the image exists.
Managed row IDs are reserved for the writer, so a hand-written row using
`android-a`, `android-b`, or `android-backup` is replaced on the next managed
install.

The `active` role is functional metadata, not presentation. BDS compares a
slot claim in an active row with the GPT active slot. If they disagree it
marks the row `SlotMismatch`, and withholds unattended launch when that row is
the configured default. Re-run an install with the correct explicit slot to
repair the label. An install with an unknown slot is refused: use `--slot a`
or `--slot b`; `--inactive` additionally requires known active-slot metadata
and its explicit safety acknowledgement.

## A/B generation lifecycle and legacy migration

An install updates the selected slot in place. Before committing the new
triplet, `canoe-bootmgr` copies that slot's existing triplet to
`boot_backup.efi` with its matching sidecars. Thus `boot_backup.efi` is the
previous generation of the last-updated slot, not a permanent third slot. If
the selected slot had no valid triplet, the backup triplet is removed. A
`--both` install updates both slots in a defined transaction; the final
backup is still the previous generation of the last slot updated.

The singular `boot.efi` name is retired from new installs. For migration,
the writer accepts a complete legacy `boot.efi` triplet and copies it to the
explicit target slot when that slot has no valid triplet, then removes the
legacy files. A valid legacy triplet is removed without copying when the target
already has a valid triplet; an incomplete legacy set is quarantined. After
migration, only the per-slot names and (when present) `boot_backup.efi` remain.
The old `boot.efi` name is retained only as a BDS compatibility probe for
pre-b2 roots; it is not a current managed install destination.

After an OTA, keep the device in the running system and use the module's
**Install to inactive slot** action before rebooting. It must receive target
slot metadata, installs only that inactive slot, and refuses to relabel or
fall back to the running slot. If it is skipped, the new slot has no managed
loader and boots stock; no configuration row can make a stock ABL load BDS.

## Sidecars and modes

Only the managed paths `boot_a.efi`, `boot_b.efi`, and `boot_backup.efi` have
sidecars interpreted by BDS. Per-image sidecars on hand-added rows are not
honoured. A row with one of the managed paths on removable media is still a
passthrough row; managed policy is for the device boot root only.

Mode 0 is a hook-free passthrough. Mode 1 projects the locked DeviceInfo view
and enables the normal managed hooks. Mode 2 additionally uses the matching
profile for that managed loader. A menu mode is a session-only override for the
next launch; it is never written to this file. A per-entry mode applies to that
entry, with global `mode` as the fallback.

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

This hand-authored example has two valid slot triplets and a previous
generation. The managed installer may choose different rows based on which
triplets exist; it does not create a default automatically.

```text
version 1
generation 4
menu-mode silent
key-window 1200
menu-timeout 5
default android-a
mode 1
devinfo-repair asneeded

entry android-a
  title Android A
  image boot_a.efi
  mode 1
  role active

entry android-b
  title Android B
  image boot_b.efi
  mode 1
  role inactive

entry android-backup
  title Android (previous)
  image boot_backup.efi
  mode 0
  role backup
```

## Empty or invalid configuration

No file, an invalid `version`, or a file with no usable entry is not itself an
error. BDS probes the known managed paths `boot.efi` (pre-b2 compatibility),
`boot_a.efi`, `boot_b.efi`, and `boot_backup.efi`, then shows the menu rather
than launching unattended when configuration is missing. An empty or
unreachable boot root with none of those paths is first run: BDS shows its
first-run screen, whose timeout/default is **Enter fastboot**; Volume Up is the
only key that opts into the normal menu so discovered rows can be inspected.
