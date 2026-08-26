# Installation Guide

## Boot Flow

The real ABL loads an embedded **superfastboot BDS** off the raw `efisp` partition (via the GBL vulnerability). The BDS then reads the boot-root configuration, shows the menu when requested, and chains to the selected entry.

On this device the boot root is the `persist` partition (ext4), under its `efisp/` directory. A booted Android system mounts it at `/mnt/vendor/persist`; a custom recovery mounts it at `/persist`. The BDS scans ext4 volumes for an `efisp/` directory and treats that as the boot root.

| File | Purpose |
|------|---------|
| `canoe.cfg` | Declarative boot policy: file-global fallback mode, per-entry modes, and entry roles |
| `boot.efi` | Patched ABL launched by a configured Android entry |
| `boot.efi.gm2p` | Matching 120-byte KeyMint profile derived from matching stock vbmeta |
| `boot.efi.tzmap` | Optional 256-byte `GTZM` ABL-derived TrustZone interface map from the unpatched ABL |
| `boot_backup.efi` / `.gm2p` / `.tzmap` | Previous complete ABL/profile/map set, retained as the backup entry |
| `tools/` | EFI tools reached from the menu's built-in `EFI Tools` row |

The format of `canoe.cfg` is normative; see [`canoe-cfg.md`](./canoe-cfg.md) rather than copying its specification into another page. `BDS.efi` is written raw to the `efisp` partition (not into a filesystem).

## 1. Prerequisite: GBL Vulnerability

The ABL on the `abl` partition must contain the **GBL vulnerability** so it loads the BDS off `efisp`. If your ABL lacks it, flash an **older ABL version** that has the vulnerability to the `abl` partition first. The patched `boot.efi` and its sidecars must still be a matching stock firmware set; they may differ from the downgraded partition ABL.

## 2. Install Methods

| Method | Description |
|--------|-------------|
| **KernelSU module (Recommended)** | Automated from a booted rooted system; the first-install flow asks for mode and the Mode 1 choices, installs the boot root, then reboots to Recovery so data can be formatted |
| **Host wizard** | Interactive `canoe`/`canoe.cmd` flow that asks first-time/update, mode, required Mode 1 choices, waits for matching stock images, and generates the boot entry |
| **Toolkit, standalone** | Host-driven over ADB from a custom recovery. Needs no firmware package, no graft, and no root on the running system |
| **Toolkit, with a firmware package** | For Super Flasher / RegionalHybrid: prepare the package's inputs, run its flasher unchanged, then install from Recovery |
| **Toolkit, one-shot** | Non-interactive temporary-root launch from a stock ABL image; writes nothing permanent |
| **Toolkit, fully manual** | Derive artifacts, write a `canoe.cfg`, place the tree, and flash the BDS by hand |

## 3. Module Install (KernelSU)

### 3.1 Fresh install

The device-side module's first-install flow is:

1. Answer that this is the first installation and choose Mode 0, 1, or 2.
2. In Mode 1, acknowledge that a custom recovery must be grafted with the vbmeta tool, flashed, and then returned to; choose whether to patch `vendor_boot`.
3. The module patches the ABL and derives matching sidecars, installs the boot root under `/mnt/vendor/persist/efisp/`, and flashes `BDS.efi` to `efisp`.
4. It prints a readable result and automatically reboots to Recovery after a countdown so you can format data.

A later module install is plain: it does not ask the first-install questions. The module and WebUI remain available after the boot chain is installed.

### 3.2 Mode selection in the WebUI

The WebUI mode selector changes a named boot entry in `canoe.cfg`; it does not write a partition record. Entry modes are per entry, with the file-global `mode` acting as the fallback. The backup entry is an ordinary third row with `role backup`, alongside the A and B rows.

### 3.3 After an OTA

The installed module includes a background watcher. It watches the `abl_a` and `abl_b` device nodes with `inotifyd`, verifies a candidate change against the digest recorded at install, and uses slow polling when `inotifyd` is unavailable. When an OTA really changes the inactive-slot ABL, the watcher re-derives that slot's pair and adds a new `canoe.cfg` entry with the correct role. It leaves the entry that currently boots in place; the previously working entry is never removed. There is no WebUI reflash step to repeat after every OTA.

## 4. Host Toolkit Install

The toolkit archives contain the same `canoelib/` Python package and one entry point. On Linux use `./canoe`; on Windows use `canoe.cmd`. With no arguments the command is the interactive wizard. Its scriptable surface is:

```text
canoe                              interactive wizard (default)
canoe build                        derive the ABL/profile/map artifacts
canoe prep [--pkg ...]             prepare a firmware package
canoe prep-device [--slot ...]     pull a device pair and derive artifacts
canoe install [--skip-bds ...]     install the boot root over ADB
canoe oneshot --abl <img> --mode 0|1
                                   temporary, non-interactive boot
```

Use the same subcommands with `canoe.cmd` on Windows. Existing options carry over unchanged under their new verb. The wizard requires the candidate `abl.img` and `vbmeta.img` to be **stock** and to match the firmware version being booted; when `images/` is empty it explains what is needed and watches the folder until both files arrive.

### 4.1 Standalone (custom recovery + ADB)

The only prerequisite is a custom recovery with ADB enabled. Persist is writable there, so no root on the running system is needed.

First prepare the pair:

```bash
./canoe prep-device        # active slot by default
# Use the other slot after adb sideload:
./canoe prep-device --slot inactive
```

On Windows:

```bat
canoe.cmd prep-device
canoe.cmd prep-device --slot inactive
```

The derived `boot.efi`, `.gm2p`, and `.tzmap` must describe one matching stock ABL/vbmeta pair. If the `abl` partition does not already contain a GBL-vulnerable ABL, flash one before booting the chain:

```bash
fastboot flash abl <vulnerable>.img
```

Then boot the custom recovery with ADB and install the prepared tree:

```bash
./canoe install
```

Use `./canoe install --skip-bds` when the BDS must not be written. The Windows equivalent is `canoe.cmd install` (or `canoe.cmd install --skip-bds`). A mode passed to install is written into the generated `canoe.cfg` entry; the BDS itself never writes that file.

### 4.2 Alongside a firmware package

For the Super Flasher / RegionalHybrid workflow, prepare the package without reimplementing its flasher:

```bash
# host side; no device needed
./canoe prep --pkg OOS_FILES_HERE \
             --recovery <custom>.img \
             --abl <vulnerable>.img \
             --in-place

# run the package's own flasher unchanged
bash Super_Flasher.sh

# boot the custom recovery, enable ADB, then install
./canoe install
```

On Windows, use `canoe.cmd prep ...` and `canoe.cmd install`. `--in-place` substitutes the prepared images into the package and keeps `<name>.img.canoe-orig` backups. The package's own slot and logical-partition handling remains its responsibility.

The package's stock `abl.img` + `vbmeta.img` pair is the pair from which the loader and `.gm2p` sidecar must be derived. The `--abl` input used by the package flasher can be the older vulnerable ABL that must remain on the `abl` partition.

### 4.3 One-shot temporary root

When the bootloader is locked and the matching stock ABL image is already known, use:

```bash
canoe oneshot --abl <img> --mode 0
# or --mode 1
```

This path is non-interactive. It expects a known stock image, obtains root for one launch, and writes nothing permanent: no boot-root configuration, boot entry, or partition state is saved.

### 4.4 Fully manual

1. Run `canoe build` with matching stock `images/abl.img` and `images/vbmeta.img`.
2. Create a `canoe.cfg` describing the entries and roles; use the normative [`canoe.cfg` format](./canoe-cfg.md).
3. Copy the complete generated `efisp/` tree and `canoe.cfg` into the boot root: `/mnt/vendor/persist/efisp/` from a booted system or `/persist/efisp/` from recovery.
4. Run `sync`.
5. Flash the BDS:

   ```bash
   dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M
   ```

## 5. Windows ext4 access

The Windows archive bundles `platform-tools`. For ext4 read/write it uses **WinFsp plus LKL `lklfuse`**. These components are fetched and SHA-256-verified on first use rather than vendored into the repository. This is the Windows path for mounting `persist` after the BDS exports it over USB Mass Storage and editing the boot root directly when ADB is unavailable.

## 6. First run and Superfastboot recovery

If the boot root has neither `canoe.cfg` nor `boot.efi`, the BDS shows a first-run screen and goes straight to Super Fastboot. There is nothing to boot, and fastboot is the only channel that can install anything.

The BDS menu includes **Reboot to Recovery** and **USB Mass Storage**. The latter exports `persist` or, when it exists, `logfs` as one normal USB disk at a time. `persist` carries the boot root and is the repair channel for a device without working ADB, so the BDS warns before exporting this live filesystem. **Volume Down** ends the session. The same feature is available from fastboot as `fastboot oem mass-storage`, `fastboot oem mass-storage:persist`, or `fastboot oem mass-storage:logfs`; the bare form means `persist`.

For full details, including the Windows mount step, see [`mass-storage.md`](./mass-storage.md). The menu's mode row is a session override for the next launch, not a saved setting; configured entry modes take precedence, and the persisted fallback is the file-global `mode` in `canoe.cfg`. See [`usage.md`](./usage.md) for the remaining Superfastboot commands.
