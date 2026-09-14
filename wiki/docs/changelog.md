# Changelog

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
