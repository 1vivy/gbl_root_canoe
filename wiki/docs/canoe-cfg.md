# `canoe.cfg` — the boot-root contract

`canoe.cfg` is the persisted BDS menu configuration. The mounted-root CLI,
application and explicit BDS default, entry-mode and boot-policy actions share this format. Normal
menu selections remain one-shot. The raw `efisp` partition contains BDS only.

## Location and syntax

| View | Path |
| --- | --- |
| Mounted `persist/efisp.fat` | `<mount>/canoe.cfg` |
| BDS container filesystem | `\canoe.cfg` |

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

In 7.0.0-b6 the boot policy is explicit. Global keys must appear before the first `entry`:

| Key | Values | Default | Meaning |
| --- | --- | --- | --- |
| `version` | `1` | required | Configuration format version |
| `generation` | `0..4294967295` | `0` | Monotonic installed-generation number |
| `menu-mode` | `silent`, `menu` | `silent` for fresh installs | Startup policy |
| `key-window` | `500..=5000` | `1200` | Silent startup Volume Up window in milliseconds |
| `menu-timeout` | `0..=300` | `3` | Menu countdown in seconds; only in Menu mode |
| `show-booting` | `yes`, `no` | `yes` | Display the Booting message when launching an image |
| `default` | an entry ID or `bls:<stem>` | none | Row launched without menu input |
| `mode` | `0`, `1`, `2` | `1` | Fallback mode for entries without their own mode |
| `devinfo-repair` | `asneeded`, `never` | `asneeded` | Whether a managed launch may repair `DeviceInfo` |

`key-window` is inclusive at both bounds and cannot be disabled. Older numeric
values are clamped on read: `key-window 0` becomes 500 ms, and values above 5000
become 5000 ms. New policy saves require 500–5000 ms. During Silent startup,
only VOL UP opens the boot menu; VOL DOWN and Power do not interrupt the wait.
Select **Enter Super Fastboot** from the menu to enter its Fastboot session;
no startup key selects it directly. Without Volume Up, Silent mode launches the
configured default. Menu mode opens the menu immediately, uses both volume keys
for navigation, and counts down for
`menu-timeout` seconds when the saved default resolves
to a boot entry. The title reads **Boot menu - Highlighted entry will boot in Xs.**
Opening the menu with Volume Up or interacting with it disables this
countdown; the title then reads **Boot menu - Timeout is disabled.** Returning from a submenu
does not restart it. An unresolved default in a populated root opens the menu
without a countdown. With no populated boot root, BDS opens this same menu with
the temporary **Entering Super Fastboot** entry highlighted and a three-second
countdown. The permanent **Enter Super Fastboot** action remains available.
Both enter the same Fastboot session; the temporary entry is not saved. There is no
separate first-run screen or additional key window. Power/Enter selects the
highlighted action; the volume keys cancel the countdown and navigate normally.
Changing the default duration does not replace an explicitly saved timeout.

The default `show-booting yes` applies to all images, including legacy `boot.efi`.
`show-booting no` suppresses the message, not image loading or diagnostics. A
menu-driven launch still clears the menu; an unattended launch preserves the
existing screen. The CBM and BDS **Hide Booting…** controls edit this one key.
Older BDS builds ignore this additive key; current firmware advertises
`canoe-boot-policy=show-booting-v1`. Configuration remains `version 1`.
A policy-only document with no boot entries is valid. Save default never changes
an entry mode; Change entry mode never changes the default target.

Here, Super Fastboot means the BDS's own fastboot session, which waives ABL's
critical-partition status so flashing works from it; partitions inside `super`
remain the exception. Stock userspace `fastbootd` is reserved for the
fresh-install path.

The writer never emits `timeout`. BDS accepts a pre-b2 `timeout N` line only as
a compatibility alias for `menu-mode menu` plus `menu-timeout N`; it is not a
rejected line and is never written by current tools.

`default` may name any resolvable `canoe.cfg` entry or a discovered BLS Type #1
row as `bls:<stem>`. The stem is the case-folded lowercase `.conf` basename and
must match `^[a-z0-9._-]{1,63}$` (for example, `loader/entries/pmOS.conf`
becomes `bls:pmos`). A BLS default remains passthrough: it has no managed
sidecars, mode hooks, or slot semantics. USB-hosted BLS rows are not eligible as
unattended defaults. If the configured default cannot be resolved, BDS opens the
menu, shows the existing rejected/notice surface, and waits; it never falls
through to another row.

Inside an entry block, `title`, `image`, `options`, `mode`, and `role` are valid.
A per-entry `mode` overrides the global fallback. File-global keys appearing in
an entry are rejected rather than retroactively applied.

The complete writer grammar is:

```text
version 1
generation N
menu-mode silent|menu
key-window 1200
menu-timeout 3
show-booting yes
default android-a          # or: default bls:pmos
mode 0|1|2
devinfo-repair asneeded|never
```

`options` is the command line handed to the image as UEFI LoadOptions. It is at
most 383 characters and is passed through byte for byte: unlike `image` it is
not a path, so `/` is never folded to `\` and a value that looks like a path is
left exactly as written. An empty `options` is a rejected line rather than a
silent no-op.

This is what lets a row hold a payload the BDS does not itself parse. BDS is a
chainloader selector: it starts a PE and hands over `options` byte for byte, and
the image on the other end owns its argument grammar entirely.

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

### Paths

The container filesystem root is the boot root. `image mu/place.efi` resolves
to `\mu\place.efi`. BLS image paths also resolve there. `options` is passed
untouched to the payload; if the payload opens a file on this volume, its path
starts at the same filesystem root. Do not add the legacy `\efisp` prefix.
Payload-specific memory addresses/options remain the payload's responsibility.

## Managed A/B triplets

The 7.0.0-b6 writer manages a loader and its two matching sidecars for each installed slot.

Each installed slot has `boot_a.efi` or `boot_b.efi`, with a matching 120-byte
`.gm2p` and 256-byte `.tzmap`. Do not mix sidecars between generations. The
application prepares selected slots independently and publishes matching rows.
The small CLI's `loader install` and `entry set` are separate explicit commands.

`role active` is functional metadata: BDS compares its slot claim with the
actual active slot and withholds unattended launch on a mismatch. Do not label
an unknown slot as active.

Recovery snapshots belong to saved application operations outside the boot
root. There is no implicit backup rotation or migration of ext4 `efisp/`.
`boot_backup.efi` and the old `boot.efi` name remain firmware compatibility
names, not automatically imported generations. See [reinstallation](./reinstall.md).

## Sidecars and modes

Only the managed paths `boot_a.efi`, `boot_b.efi`, and `boot_backup.efi` have
sidecars interpreted by BDS. Per-image sidecars on hand-added rows are not
honoured. A row with one of the managed paths on removable media is still a
passthrough row; managed policy is for the device boot root only.

Mode 0 is honest-unlocked, with the universal efisp recursion guard. Mode 1 projects the locked DeviceInfo view
and enables the normal managed hooks. Mode 2 additionally uses the matching
profile for that managed loader. A menu mode is a one-shot override unless explicitly saved using Save as default. A per-entry mode applies to that
entry, with global `mode` as the fallback.

A successful Mode 2 derivation means only that `vbmeta` parsed and carries a
signature and public-key blob. It does not prove that the key is the OEM's; no
tool here can prove that. The only automatic protection is detecting whether the
public-key digest changed since the last installed generation. A changed signer
is expected when moving to or from a custom ROM and requires the operator's
explicit allowance for that supplied firmware.

## DeviceInfo repair

A Mode 1 or Mode 2 launch may repair `DeviceInfo` when the observed state does
not satisfy the requested mode. `devinfo-repair asneeded` permits that repair;
`devinfo-repair never` refuses it and continues honestly in Mode 0. Mode 0
neither reads nor writes `DeviceInfo`. The boot log records the observation and
the chosen action before the decision.

## Example

This hand-authored example has two valid slot triplets and a previous generation.
The managed installer may choose different rows based on which triplets exist; it
does not create a default automatically.

```text
version 1
generation 4
menu-mode silent
key-window 1200
menu-timeout 3
show-booting yes
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
than launching unattended when configuration is missing. If the boot root is
absent or empty, the same menu highlights the temporary **Entering Super Fastboot**
entry with a three-second countdown. Power/Enter selects it; the volume keys cancel the
countdown and navigate the menu. Filesystem access failures remain a separate
unavailable state, not evidence of an empty installation.

A missing or malformed current configuration can use the validated
`canoe.cfg.prev`. Permission and I/O failures do not trigger fallback. Saving
publishes the previous configuration before replacing the current file.
