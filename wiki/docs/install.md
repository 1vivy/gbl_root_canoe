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

The format of `canoe.cfg` is normative; see [`canoe-cfg.md`](./canoe-cfg.md). `BDS.efi` is the only whole-partition image in this chain and is written raw to `efisp`; the boot root is a directory in the live `persist` filesystem.

There is exactly one `canoe.cfg` writer: `tools/canoe-device/canoe_boot_entry.sh`. The host toolkit, the KernelSU module, and the OTA watcher all call this same script. Its `set` operation is an UPSERT: it replaces the named entry in place and preserves every other entry, including hand-added custom-ROM entries.

## 1. Prerequisite: GBL Vulnerability

The ABL on the `abl` partition must contain the **GBL vulnerability** so it loads the BDS off `efisp`. If your ABL lacks it, flash an **older ABL version** that has the vulnerability to the `abl` partition first. The patched `boot.efi` and its sidecars must still be a matching stock firmware set; they may differ from the downgraded partition ABL.

## 2. The two install bundles

| Bundle | Channel | Contents |
|--------|---------|----------|
| **1. Bootloader** | Host fastboot only | Flash a vulnerable ABL only when needed, then flash `BDS.efi` to raw `efisp` |
| **2. Boot root** | Host mass-storage or device ADB | Install/refresh the boot root under `persist/efisp`, then derive the slot triplet and UPSERT its `canoe.cfg` entry |

Bundle 1 is:

```bash
# Only when the installed ABL lacks the GBL vulnerability:
fastboot flash abl <vulnerable>.img
fastboot flash efisp BDS.efi
```

This bundle is host-only, uses no Android and requires no kernel write permission. The first command is omitted when the installed ABL already has the vulnerability.

Bundle 2 is one shared implementation. Over ADB, it runs from custom recovery or a rooted system and writes the boot root on the device. Over the BDS `oem mass-storage:persist` export, it runs on the host against the mounted persist filesystem. The same boot-entry generation is also the OTA update path.

The boot root cannot come from fastboot: `persist` is a live ext4 filesystem holding vendor calibration, so `fastboot flash persist` would replace the filesystem. The raw BDS is the only part of the chain that is a whole-partition image.

## 3. Install Methods

| Method | Description |
|--------|-------------|
| **KernelSU module (Recommended)** | Automated from a booted rooted system; the first-install flow asks for mode and the Mode 1 choices, installs Bundle 2, then performs the device-side Bundle 1 write |
| **Host wizard** | Interactive `canoe`/`canoe.cmd` flow that asks first-time/update, mode, required Mode 1 choices, waits for matching stock images, and generates the boot entry |
| **Toolkit, standalone** | Host-driven over ADB from a custom recovery, or over the BDS mass-storage export |
| **Toolkit, with a firmware package** | For Super Flasher / RegionalHybrid: prepare the package's inputs, run its flasher unchanged, then install Bundle 2 |
| **Toolkit, one-shot** | Non-interactive temporary-root launch from a stock ABL image; writes nothing permanent |

## 4. Module Install (KernelSU)

The kernel-write prerequisite applies only to this path: the module must be able to write `abl` and raw `efisp`; Baseband Guard blocks those writes. The host toolkit route does not need kernel write permission because its bootloader bundle uses fastboot.

### 4.1 Fresh install

The device-side module's first-install flow is:

1. Answer that this is the first installation and choose Mode 0, 1, or 2.
2. In Mode 1, acknowledge that a custom recovery must be grafted with the vbmeta tool, flashed, and then returned to; choose whether to patch `vendor_boot`.
3. The module patches the ABL and derives matching sidecars, installs Bundle 2 under `/mnt/vendor/persist/efisp/`, and performs the device-side raw BDS write.
4. It prints a readable result and automatically reboots to Recovery after a countdown so you can format data.

A later module install is plain: it does not ask the first-install questions. The module and WebUI remain available after the boot chain is installed.

### 4.2 Mode selection in the WebUI

The WebUI mode selector asks the shared `canoe_boot_entry.sh` writer to change a named boot entry in `canoe.cfg`; it does not write a partition record. Entry modes are per entry, with the file-global `mode` acting as the fallback. The backup entry is an ordinary third row with `role backup`, alongside the A and B rows.

### 4.3 After an OTA

The installed module includes a background watcher. It watches the `abl_a` and `abl_b` device nodes with `inotifyd`, verifies a candidate change against the digest recorded at install, and uses slow polling when `inotifyd` is unavailable. When an OTA really changes the inactive-slot ABL, the watcher re-derives that slot's pair and asks the same `canoe_boot_entry.sh` writer to UPSERT a new `canoe.cfg` entry with the correct role. It leaves the entry that currently boots in place; the previously working entry is never removed. There is no WebUI reflash step to repeat after every OTA.

## 5. Host Toolkit Install

The toolkit archives contain the same `canoelib/` Python package and one entry point. On Linux use `./canoe`; on Windows use `canoe.cmd`. With no arguments the command is the interactive wizard. Its scriptable surface is:

```text
canoe                              interactive wizard (default)
canoe build                        derive the ABL/profile/map artifacts
canoe prep [--pkg ...]             prepare a firmware package
canoe prep-device [--slot ...]     pull a device pair and derive artifacts
canoe install [--via adb|mass-storage] [--boot-root PATH]
                                   install the boot root and upsert its boot entry
canoe oneshot --abl <img> --mode 0|1
                                   temporary, non-interactive boot
```

Use the same subcommands with `canoe.cmd` on Windows. The wizard requires the candidate `abl.img` and `vbmeta.img` to be **stock** and to match the firmware version being booted; when `images/` is empty it explains what is needed and watches the folder until both files arrive.

### 5.1 Prepare the matching pair

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

The derived `boot.efi`, `.gm2p`, and `.tzmap` must describe one matching stock ABL/vbmeta pair. If the `abl` partition lacks the GBL vulnerability, complete Bundle 1 before booting the chain:

```bash
fastboot flash abl <vulnerable>.img
fastboot flash efisp BDS.efi
```

Omit the first command when the installed ABL already has the vulnerability. The second command is always the raw BDS flash in the host flow.

### 5.2 Bundle 2 over ADB

Boot custom recovery with ADB, or use a rooted Android system, then run the shared transaction on the device:

```bash
./canoe install --via adb
```

The default route is ADB, so `./canoe install` remains equivalent. To select an
already-mounted host boot root explicitly, use the local mount route:

```bash
./canoe install --boot-root <persist-mount>/efisp
```

### 5.3 Bundle 2 over BDS mass storage

`./canoe install --via mass-storage` asks the running BDS to perform
`fastboot oem mass-storage:persist`, waits for the USB disk, mounts `persist`
read-write, and runs the same `canoe_device_install.sh` locally against the
mounted boot root:

```bash
./canoe install --via mass-storage
```

For an already-mounted filesystem, including the Windows WinFsp + LKL `lklfuse` route, pass the mount or its `efisp` directory:

```bash
./canoe install --boot-root <persist-mount>
```

After the host flushes and safely unmounts `persist`, press **Volume Down** on the device to end the export session. Unplugging or losing the USB link does not end it; replugging resumes the same session.

### 5.4 Alongside a firmware package

For the Super Flasher / RegionalHybrid workflow, prepare the package without reimplementing its flasher:

```bash
# host side; no device needed
./canoe prep --pkg OOS_FILES_HERE \
             --recovery <custom>.img \
             --abl <vulnerable>.img \
             --in-place

# run the package's own flasher unchanged
bash Super_Flasher.sh

# complete Bundle 1 (run the first command only if needed), then boot custom recovery and run Bundle 2
fastboot flash abl <vulnerable>.img
fastboot flash efisp BDS.efi
./canoe install --via adb
```

On Windows, use `canoe.cmd prep ...`, the package's own flasher, and `canoe.cmd install --via adb`. `--in-place` substitutes the prepared images into the package and keeps `<name>.img.canoe-orig` backups. The package's own slot and logical-partition handling remains its responsibility.

### 5.5 One-shot temporary root

When the bootloader is locked and the matching stock ABL image is already known, use:

```bash
canoe oneshot --abl <img> --mode 0
# or --mode 1
```

This path is non-interactive. It expects a known stock image, obtains root for one launch, and writes nothing permanent: no boot-root configuration, boot entry, or partition state is saved.

## 6. Windows ext4 access

The Windows archive bundles `platform-tools`. For ext4 read/write it uses **WinFsp plus LKL `lklfuse`**. These components are fetched and SHA-256-verified on first use rather than vendored into the repository. This is the Windows path for mounting `persist` after the BDS exports it over USB Mass Storage and editing the boot root directly when ADB is unavailable.

## 7. First run and Superfastboot recovery

If the boot root has neither `canoe.cfg` nor `boot.efi`, the BDS shows a first-run screen and goes straight to Super Fastboot. There is nothing to boot, and fastboot is the only channel that can install anything.

The BDS menu includes **Reboot to Recovery** and **USB Mass Storage**. The latter exports `persist` or, when it exists, `logfs` as one normal USB disk at a time. `persist` carries the boot root and is the repair channel for a device without working ADB, so the BDS warns before exporting this live filesystem. **Volume Down** ends the session. The same feature is available from fastboot as `fastboot oem mass-storage`, `fastboot oem mass-storage:persist`, or `fastboot oem mass-storage:logfs`; the bare form means `persist`.

For full details, including the Windows mount step, see [`mass-storage.md`](./mass-storage.md). The menu's mode row is a session override for the next launch, not a saved setting; configured entry modes take precedence, and the persisted fallback is the file-global `mode` in `canoe.cfg`. See [`usage.md`](./usage.md) for the remaining Superfastboot commands.
