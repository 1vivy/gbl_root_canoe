# Changelog

## 7.0.9

### Myron support (untested)

Mode 2 profile derivation now falls back to `init_boot` AVB properties when
the corresponding `boot` properties are absent, supporting root vbmeta images
such as Myron's. Myron support has not been tested on hardware.

### DICE spoofing

Prepared ABLs now report Normal DICE mode on both BCC paths. Mode 2 users get
DICE spoofing, which should improve compatibility with Google's Remote Key
Provisioning (RKP).

Thanks to [@NullCode1337](https://github.com/NullCode1337) for bringing this up.

### Surfacer transient menu target

`fastboot oem boot-direct <selector> menu` now hands a managed Android ABL row
the private one-shot `surfacer-menu` BCB command. Surfacer consumes and clears
the command before waiting in its firmware menu. The same `menu` tag is
available to `boot-once`; it leaves fifteen selector bytes in the 32-byte BCB
command field. Recovery, status, stage and reserved BCB bytes remain unchanged.

## 7.0.8

### Boot-once targets and direct launch

Boot-once records gained a tagged form and a direct-launch verb. `fastboot oem
boot-once <selector> [<target>]` and `fastboot oem boot-direct <selector>
[<target>]` join the 7.0.5 colon form and the clear verb. Only `recovery` and
`fastbootd` are accepted as targets, only a managed Android ABL row may carry
one, and only `boot-direct` also accepts the literal `default`; it resolves and
launches without a reset and never arms a record. A tagged record reads
`canoe-once:<selector>+<target>` in the same 32-byte BCB command field, so it
budgets 11 selector bytes for `recovery` and 10 for `fastbootd` instead of the
untagged 20. An oversized selector is refused before anything is written.
Consumption reports two mutually exclusive outcomes through one reserved notice
row: an unavailable target leaves normal policy in effect, while a failed launch
after the target command was written leaves that command pending in `misc` for
the next boot.

### Super Fastboot USB receive

Fastboot receive requests are now serialized across gadget reconnects,
connection events and response completions. Those paths previously could queue
multiple host-to-device transfers against the same buffer after a mass-storage
export, allowing a later command to overwrite one still awaiting completion.
This source fix has not yet been qualified on the phone; a fresh SFB session
remains the safe starting point for device experiments.

### MdTools

`MdTools`' descriptor-wide minidump search is replaced with bounded SMEM item
602 discovery. It flushes a durable stage marker before the first firmware
lookup and limits each mutation to one owned subsystem slot. A new interactive
shadow submenu enumerates validated AOP/BOOT regions and can register one
selected live payload under a unique alias with encryption not required before
the terminal collection trigger. The shadow array remains inside MdTools RAM and
deliberately does not carry into Android userspace. Both probe tools use the
captured `TZ_DDR` base instead of the AES-key base.

### Manager

Canoe Boot Manager 7.0.8 packages this firmware and has no manager-side changes.

## 7.0.7

### Boot menu notices and slots

Notice rows (skipped config lines, an unavailable boot-once target, a fallback to
the previous configuration, a default for the other slot) carry a `!` marker and
explain themselves on the selection line. The entry for the slot currently
booted is labelled `(current slot)`, computed live. After an OTA, BDS compares
the default entry's slot, derived from its id or image, with the active slot
rather than stored roles, so saving the current slot's entry as the default is
the whole fix. `active` and `inactive` roles are still parsed for older configs
but are no longer written or shown; `(backup)` still is.

### KernelSU module updates

The module's `module.prop` points `updateJson` at the latest firmware release.
The manager's mirror workflow attaches `update.json` beside the KSU ZIP there,
so KernelSU can offer module updates in place.

### Manager

- OTA preparation offers making the target slot's entry the default, on by
  default, so the phone boots the updated slot unattended. Managed entries no
  longer carry an active/inactive role. The inputs warn that root must be
  reinstalled on the target slot before rebooting.
- Inspection reports the kernel release and the `slot_suffix` and
  `secure_user_mode` values from `/proc/cmdline` and, separately, from
  `/proc/bootconfig`.

### Fixes

- The one-shot installer writes the manager's Android entry shape: title
  `Android - Slot A` with `androidboot.slot_suffix=_a`, instead of `Android a`
  with no options. Its NDK guard also accepts `ANDROID_NDK_HOME` and
  `ANDROID_NDK_ROOT`.
- Firmware CI runs its jobs in parallel, and a release tag reuses the verified
  build of the same commit from `main`.

## 7.0.6

### Fastbootd in Mode 2

The patched ABL can flash from bootloader fastboot, but userspace Fastbootd
reads the lock state the managed launch projects: under Mode 1 it sees a locked
device and refuses to flash. BDS now reads the Android BCB command at startup;
when it is exactly `boot-fastboot`, managed Android launches in that session
request Mode 2 regardless of the entry's mode. The read never consumes or
rewrites `misc`, so ABL and recovery still enter Fastbootd. Other reboot
targets keep the configured mode.

`canoe.cfg` gains `fastbootd-mode2 yes|no` (default `yes`); `no` disables the
override. It is exposed in the BDS boot policy menu, as
`canoe-bootmgr config set-policy --fastbootd-mode2`, and as **Use Mode 2 for
Fastbootd** in the manager's Settings. The manager only edits it through a
worker that advertises `boot-policy-fastbootd-mode2-v1`, because an older worker
would drop the key on save.

### One-shot Android installer

The retired Android, Linux and Windows toolkit targets are replaced by
`canoe-one-shot-<version>-android-arm64.zip`, a rooted-Android installer built
from the same firmware output and attached to each firmware release.

### Fixes

- Cleared the read-only flag Android's updater leaves on verified partitions
  before writing, so the updated slot's `abl` can be written after an OTA
  without a reboot. A Baseband Guard (BBG) kernel denial now reports BBG's
  kernel-log line and the command-line values its allowlist uses.
- Gave `persist/efisp.fat` the persist folder's SELinux label. Hosted builds
  created it unlabeled, so on 6.12 kernels before 6.12.25, which SELinux-check
  loop I/O, KSU boot-file work failed with EIO. New containers copy the parent's
  label; the worker labels an existing unlabeled container before attaching its
  loop and leaves other labels unchanged.

## 7.0.5

### Super Fastboot entry

**Advanced** can write a Super Fastboot row into `canoe.cfg`, so it appears in
the menu like any other entry. Adding and arming stay separate: the row only
exists until it is deliberately saved as the default, which makes the phone
enter Super Fastboot unattended. The edit preserves comments, unknown keys and
every other entry, and refuses a second resident row, a taken id, a full table
or no room to append. The manager's entry editor can add the same row.

### Boot once

A one-shot launch is recorded as `canoe-once:<selector>` in the 32-byte Android
BCB command field in `misc`, leaving `canoe.cfg` untouched and later BCB bytes
intact. Selectors resolve like `default`: an entry id, `bls:<stem>` or a
fastboot action. The record is cleared and flushed before anything is resolved
or launched, so a crashing target cannot re-arm itself. Arm it from the
Advanced menu, `fastboot oem boot-once`, or a rooted Android shell; `reboot
recovery` and `reboot bootloader` overwrite the same field, so restart normally
afterwards.

Super Fastboot also publishes a volatile source descriptor (base, size and
SHA-256) for an EFI image booted from RAM, for consumers that must verify the
bytes they were started from.

### Manager

- Settings can restart the phone into system, recovery or Super Fastboot. On
  Android, Super Fastboot arms the boot-once record and then restarts normally.
- Settings offers the saved persist backup, plus an optional copy with only
  `/efisp.fat` and `/efisp` removed.
- A fresh deployment no longer defaults to Mode 2: Prepare stays disabled until
  a mode is chosen, unless the phone's observed mode pre-selects it.
- The KSU module no longer ships the ABL catalogue; ABL images are always chosen
  by the operator.

### Installer

`install-canoe.sh` installs Canoe from a root shell on a phone whose active slot
already carries the signed vulnerable ABL. It writes only raw `efisp`, never an
ABL partition, requires an explicit mode, and only plans until `--apply`.

### Fixes

- Session logs rotate on a sequence counter in each header instead of FAT
  timestamps, which have no RTC that early in boot and caused each new session
  to overwrite the previous one.
- The added Super Fastboot row can be saved as the default, a device without
  `canoe.cfg` can add it, and it is titled apart from the built-in action.

## 7.0.1

First stable release of the 7.x line. It supersedes the `7.0.0-b1` … `7.0.0-b7`
beta run; no 7.0.0 stable was published. Everything below is measured against
**6.3.5**, the last stable release.

7.0.1 is not an in-place upgrade of 6.x. The boot root, the configuration
format, the managed loader layout and the whole host surface were replaced.
Existing installations are not migrated — see
[Reinstall from gbl-chainload, gbl_root_canoe 6.3.5, or another EFISP
mod](./reinstall.md).

### Boot root

6.3.5 kept a `BOOTENTRIES` list and a single `boot.efi` set inside the ext4
`persist/efisp/` directory, and BDS scanned compatible ext4/FAT partitions for
entries.

7.0.1 owns a dedicated FAT16 container at `persist/efisp.fat` and mounts it
itself. The container is allocated from free persist space in 8 MiB increments
between 8 and 256 MiB; the running BDS advertises the range it supports, and
older `fat16-container-v1` firmware understands only its fixed 32 MiB geometry.
The mounted root holds `canoe.cfg`, the managed loaders, the `tools/` directory
and any manually installed BLS entries. No GPT change is involved.

The legacy `persist/efisp/` directory is ignored and never imported, and nothing
inside it is removed implicitly. Clear it deliberately *before* provisioning:
the container is sized from the free space persist reports, minus a headroom
margin and rounded down to an 8 MiB step, so remnants shrink the result. The app
offers this as a reviewed cleanup.

### Configuration

`BOOTENTRIES` is replaced by `canoe.cfg` (`version 1`): a documented 7-bit ASCII
grammar, at most 8192 bytes and 24 entries, with an over-long line skipped
rather than truncated. See [the boot-root contract](./canoe-cfg.md).

Boot policy is now explicit instead of implied:

| Key | Values | Default |
| --- | --- | --- |
| `menu-mode` | `silent`, `menu` | `silent` for fresh installs |
| `key-window` | 500–5000 ms | 1200 |
| `menu-timeout` | 0–300 s, Menu mode only | 3 |
| `show-booting` | `yes`, `no` | `yes` |
| `default` | an entry id or `bls:<stem>` | none |
| `mode` | `0`, `1`, `2` | `1` |
| `devinfo-repair` | `asneeded`, `never` | `asneeded` |

Ordinary menu selections apply to that boot only. Only the explicit
Save-as-default, entry-mode and boot-policy actions write the file, and each
stages, flushes and preserves a validated `canoe.cfg.prev`. An unresolvable
default opens the menu instead of silently launching another row.

### Managed loaders

6.3.5 managed one `boot.efi` with its matching `.gm2p` and `.tzmap`. 7.0.1
manages a per-slot set — `boot_a.efi`, `boot_b.efi` and `boot_backup.efi` — and
every generation carries its own 120-byte `.gm2p` and 256-byte `.tzmap`.
Generations must not be mixed. `boot.efi` is still read as a pre-b2
compatibility name.

Prepared managed ABL images now disable their own efisp lookup through the ABL
patcher rather than relying on a runtime Block I/O hiding hook, so an unmodified
on-slot ABL cannot be assumed safe to chainload.

### Boot menu and startup

One renderer and one navigation runner serve the boot menu, every submenu and
the EFI file browser. Either volume key opens the menu; Power/Enter selects.
Actions are grouped into USB Mass Storage and Enter Super Fastboot, an
**Advanced** submenu (save a default, change an Android entry's mode, boot
policy, Android EFI tools, select an EFI file), a **Reboot** submenu (Fastbootd,
Bootloader, Recovery, System), plus Power off and Restart.

An empty or absent boot root opens that same menu with a temporary **Entering
Super Fastboot** row highlighted on a three-second countdown, instead of a
separate first-run screen. An unreadable filesystem is reported separately and
is not classified as a new installation.

### Chainloading

BDS reads BLS Type #1 entries and publishes an initrd and device tree for
EFI-stub kernels, and can launch arbitrary EFI applications. A `default` may
name a discovered BLS row as `bls:<stem>`.

BDS no longer ships payload loaders. The fixed-address firmware-descriptor
loader and the Android boot-image parser were both removed: a Project Mu style
descriptor must be placed after `ExitBootServices`, which is the payload's job,
not a selector's. BDS starts a PE and hands over `options` byte for byte.
BLS and removable-media rows are passthrough launches, without the Mode 1/2
policy hooks.

### Host surface

6.3.5 shipped Linux and Windows Python toolkits plus a module installer driven
by volume keys. 7.0.1 replaces all of it:

- a hosted browser application at
  [canoe-boot-manager.1vv.ca](https://canoe-boot-manager.1vv.ca), using
  Rust/WASM for policy and image primitives and WebUSB for fastboot; and
- a KernelSU module whose WebUI X serves the same application locally with a
  native Android root worker.

Module installation installs the manager only. It never deploys CANOE-BDS,
touches boot partitions, imports a previous modification or formats data, and
there is no volume-key deployment dialog. Desktop executables and filesystem
sidecars are retired; `target_toolkit_linux` and `target_toolkit_windows` now
refuse to build, and the `7.0.0-b4-final` tag preserves the preceding stack.

Fresh installation requires an independently saved persist backup before any
direct managed-USB ext4 edit.

### Commands

The interactive `canoe` wrapper and the Android `build.sh` workflow are retired
in favour of explicit commands: `canoe-bootmgr` for an already mounted boot
root, `canoe-image` for supplied images, and `canoe-provision` for the
container. `canoe-manager` is the Android native root worker. Each validates its
inputs, reports write and flush failures, and accepts `--json`.

### Super Fastboot and USB

Super Fastboot is BDS's own fastboot session. It waives ABL's
critical-partition status, so flashing works from it — partitions inside `super`
remain the exception. It accepts `fastboot boot <image>.efi` without a manually
prepared Android boot-image wrapper, supports Android/recovery/bootloader/
userspace-fastbootd reboot targets, and exports storage over USB through
`fastboot oem mass-storage:<target>`. Three targets exist: `boot-root` as
ordinary removable FAT storage, raw `persist`, and `logfs`. Raw persist export
is a separate, deliberate operation.

### Firmware artifacts and release pipeline

Firmware CI builds `BDS.efi` plus eight standalone EFI tools — `ArbTools`,
`BLTools`, `RebootTools`, `SurfaceTools`, `UsbTools`, `LogTools`, `MdTools` and
`CrashTools` — and publishes `manifest.json` and `SHA256SUMS` alongside them.
Releases are always created as drafts and never published automatically;
a version carrying a `-suffix` is additionally marked prerelease, so 7.0.1
drafts as an ordinary release. `imports.toml` declares every imported source,
artifact, fetch and external input, and `make version-check` gates them together
with the generated version files and the BDS build stamp.

### Fixes

- Backported upstream CRC16 fixes for ext4 group descriptors.
- Stopped signalling `ReadyToBoot` and `EndOfDxe` when starting the FAT stack.
- Fixed fastboot reply framing and surfaced boot-root mount failures.
- Kept container mounts inside filesystem driver lifetimes and preserved the
  original mount error during cleanup.
- Allowed kernel loop I/O to the persist boot root, and kept module policy
  comments readable by strict parsers.
- Built BDS from clean objects so a stale header limit cannot survive a rebuild.
- Corrected `vendor_boot` patch preparation.
- Reported the observed `DeviceInfo` state and the action taken on it, rather
  than leaving the decision implicit.
- Published the last-boot launch record under a checked contract, cleared on BDS
  entry, before launch, and on child return or menu/fastboot re-entry.

### Hardening

- The active slot's retry counter is reset from a fresh partition-table read, so
  repeated boot attempts do not accumulate into an automatic slot swap. The
  write refuses to proceed unless exactly one matching `abl_a`/`abl_b` entry is
  marked active.

## 6.3.5 and earlier

Historical 6.x material is recorded in
[ARCHIVE.md](https://github.com/1vivy/gbl_root_canoe/blob/main/ARCHIVE.md).
