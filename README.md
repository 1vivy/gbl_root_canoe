# GBL Root Canoe

[中文版](README_zh.md)

> ⚠️ **This project has been archived.** This is the final version — the patching engine is stable across multiple vendors and ABL versions, the core logic no longer changes, and active maintenance has ended. The code still works; forks are welcome. See [ARCHIVE.md](ARCHIVE.md) for details.

`gbl_root_canoe` is an EDK2-based workspace for patching the EFI applications within Qualcomm ABL (Android Bootloader) images. It leverages a GBL (Generic Bootloader Loader) vulnerability so the real ABL loads an embedded **superfastboot BDS** off the raw `efisp` partition. The BDS then scans a compatible partition (ext4/fat32) for boot entries and chains to the selected one - primarily to achieve a **Fake Locked Bootloader** state on Snapdragon 8 Gen 5 / 8 Elite (Gen 5) devices to bypass bootloader unlock detection.

`BDS.efi` is written raw to the `efisp` partition; the patched ABL/profile/map set (`boot.efi`, `boot.efi.gm2p`, and `boot.efi.tzmap`) and the boot entry list (`BOOTENTRIES`) live on the `persist` partition under its `efisp/` directory.
`boot.efi.gm2p` is the 120-byte KeyMint profile derived from the matching stock vbmeta; `boot.efi.tzmap` is the 256-byte `GTZM` ABL-derived TrustZone interface map derived from the **unpatched ABL**. The `.tzmap` is generated locally by the build scripts or installer, not shipped inside the archive, and is stored beside the launched image at `/mnt/vendor/persist/efisp/boot.efi.tzmap`.

---

## Builder Guide

This section is for developers who want to compile the toolkits from source.

### Prerequisites
You must be on a **Linux** host to build the project:
- `gcc` / `clang`, `lld`, `make`, `zip`, `python3`
- Rust toolchain (`cargo`/`rustup`) for the native and cross-compilation targets
- `liblzma-dev` (for compiling `extractfv`)
- **Android NDK** (Required for `make target_magisk_module` to cross-compile tools for Android)
- **MinGW-w64**

### Build Targets

**Note:** You **do not** need to provide an `abl.img` to build the distributable toolkits or module.

- **`make target_toolkit_linux`**
  Builds the superfastboot BDS (`BDS.efi`) from the `uefi` submodule and compiles the toolkit utilities (`extractfv`, `patch_abl`, `mode2_profile`, `abl_tzmap`) for Linux. `abl_tzmap` derives and validates the local 256-byte `boot.efi.tzmap` from the unpatched ABL.

- **`make target_toolkit_windows`**
  Same as above, but cross-compiles the utilities (`extractfv.exe`, `patch_abl.exe`, `mode2_profile.exe`, `abl_tzmap.exe`) into Windows `.exe` programs using MinGW-w64.

- **`make target_magisk_module`**
  Cross-compiles the toolkit utilities (`extractfv`, `patch_abl`, `mode2_profile`, `abl_tzmap`) for Android using your NDK, builds the BDS, and packages everything as a KernelSU/Magisk module.

- **`make target_toolkit_android`**
  Produces a standalone Android arm64 toolkit (`toolkit_android.zip`) with Android-native binaries (`extractfv`, `patch_abl`, `mode2_profile`, `abl_tzmap`) for on-device use outside of the module.

---

## User Guide

For more detailed instructions, please refer to the [Wiki](https://github.com/1vivy/gbl_root_canoe/wiki).

### 1. Using the Module (On-Device)

The module is designed to run directly on your rooted Android device.

**Requirements:**
- Device must be Snapdragon 8 Gen 5 / 8 Elite (Gen 5).
- Bootloader must be unlocked.
- Kernel must NOT have Baseband Guard.
- The ABL on the `abl` partition must contain the GBL vulnerability. If it does not, flash an older ABL with the vulnerability first; `boot.efi` and its matching `boot.efi.gm2p` profile still describe the current stock ABL/vbmeta pair, while the optional `boot.efi.tzmap` describes the unpatched ABL used to produce `boot.efi`; neither needs to match the downgraded `abl` partition.

**Installation & Usage:**
When flashing the module via a root manager (KernelSU, Magisk, or APatch), the script interacts with you using the volume keys:
- **Volume Up (First-time installation):** The script extracts and patches the current-slot ABL, derives `boot.efi.gm2p` from the matching current-slot vbmeta, generates the local `boot.efi.tzmap` from the unpatched ABL, installs the validated pair plus map, `BOOTENTRIES` and tools under `/mnt/vendor/persist/efisp/`, and flashes `BDS.efi` to `efisp`. The `.tzmap` is generated locally and is not shipped inside the module archive. After this, reboot into Recovery and **format Data**. Once booted, install this module again (Volume Down the second time) to complete the installation.
- **Volume Down (post-format or module-only install):** Skips boot-chain writes and completes module/WebUI installation. After each OTA, open the WebUI and flash again to retain the BL version.

### 2. Using the PC Toolkits (Linux / Windows)

If you downloaded the `target_toolkit_linux` or `target_toolkit_windows` zip files:
1. Extract the toolkit zip on your PC.
2. Place the matching stock `abl.img` and `vbmeta.img` inside the toolkit's `images/` directory.
3. **Linux:** Run `bash build.sh`. **Windows:** Run `build.bat`.
4. The script extracts and patches the ABL, outputting `efisp/boot.efi`, its matching 120-byte `efisp/boot.efi.gm2p` profile derived from the matching stock vbmeta, and the local 256-byte `efisp/boot.efi.tzmap` map derived from the **unpatched ABL**, plus `ABL_original.efi` (original). The `.tzmap` is not shipped inside the toolkit archive. `BDS.efi` is bundled. Check `patch_log.txt` - if it says "Warning: Failed to patch ABL GBL", the ABL lacks the vulnerability and the `abl` partition must be downgraded to an older ABL with it.

Both toolkits then install over ADB from a custom recovery, where `persist` is writable and no root on the running system is needed. Linux ships `.sh`, Windows ships `.bat`; options and behaviour are identical. Two independent pathways, documented in `README.canoe.md` inside the archive and in the [Wiki](https://github.com/1vivy/gbl_root_canoe/wiki):

- **Standalone** - needs only a custom recovery with ADB. `canoe_prep_device` pulls the `abl`/`vbmeta` pair off the device and derives the triplet; `canoe_stage` installs the persist tree and writes the BDS. No firmware package and no vbmeta graft are involved. If the `abl` partition is not already a GBL-vulnerable version, flash one yourself with `fastboot flash abl <vulnerable>.img`. The pair is pulled from the active slot by default; pass `--slot inactive` to source from the slot that is not currently booted, e.g. the one an `adb sideload` has just written during a custom-ROM install.
- **With a firmware package** (Super Flasher / RegionalHybrid, which ship both `.sh` and `.bat`) - `canoe_prep --pkg <dir> --recovery <custom>.img --abl <vulnerable>.img --in-place` grafts the package's official recovery vbmeta onto your custom recovery and substitutes the prepared images into the package, keeping `.canoe-orig` backups. The package's own flasher then runs unmodified, after which `canoe_stage` completes the install.

`canoe_stage` is a thin driver: it validates and stages, then hands the transaction to `canoe_device_install.sh` running on the device, so both platforms share one implementation of the rollback. Everything the commit overwrites is snapshotted first - the live triplet, the previous backup, `BOOTENTRIES` and `tools/` - the previous generation is demoted to `boot_backup.efi` (selectable from the BDS menu), the persist tree is synced before the BDS is written, the BDS write is backed up and verified byte-for-byte, and any failure rolls the whole set back. It never touches the `abl` partition, and writes the preferred-mode record only when `--mode 0|1|2` is passed (an on-device, reread-verified `mode2_profile mode-write` via the shipped `bin/mode2_profile-arm64`).

**Manual flow** (either platform, see the [Wiki](https://github.com/1vivy/gbl_root_canoe/wiki) for full steps): copy the `efisp/` tree, including `boot.efi`, `boot.efi.gm2p`, `boot.efi.tzmap`, and `BOOTENTRIES`, into the persist boot root (`/mnt/vendor/persist/efisp/` from a booted system, `/persist/efisp/` from recovery), `sync`, and flash `BDS.efi` to `efisp` (`dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M`).

### 3. OTA Upgrade
Before rebooting for an OTA update, use the module WebUI to flash and retain the old ABL version. "Update efisp" is enabled by default; for a major version upgrade keep it on, otherwise the device may get stuck on the first boot screen.

### 4. Superfastboot Usage Instructions
When OEM Unlocking is enabled and the white warning text appears on boot, press **Volume Up** to enter Superfastboot mode (the BDS).
Common commands include:
- **Temp-boot BDS in RAM (nothing is written to flash):**
  ```bash
  fastboot stage <BDS.efi>
  fastboot oem boot-efi
  ```
- **Lock and Unlock (BL related)**:
  - Lock BL, triggers a data wipe: `fastboot flashing lock`
  - Unlock BL, no data wipe: `fastboot flashing unlock` or `fastboot flashing unlock_critical`
  - *Note: If the TEE status is inconsistent, the device will refuse to provide the data key, rendering data inaccessible.*
- **Flashing and Erasing**:
  - `fastboot flash <partition> <file.img>`
  - `fastboot erase <partition>`
- **Rebooting**:
  - `fastboot reboot bootloader` (Next normal boot enters Official Fastboot)
  - `fastboot reboot recovery`
  - `fastboot reboot`

### 5. File Reference
1. `BDS.efi`: The superfastboot BDS, flashed raw to the `efisp` partition.
2. `boot.efi` / `boot.efi.gm2p` / `boot.efi.tzmap`: The patched ABL, its matching 120-byte locked/green KeyMint profile from stock vbmeta, and the 256-byte TrustZone map from the unpatched ABL, placed on `persist` under `efisp/`; the map is generated locally and is not shipped in the archive.
3. `LinuxLoader.efi` / `ABL_original.efi`: The original unpatched ABL. For analysis; do not flash to `efisp`.
4. `BOOTENTRIES`: Boot entry list, format `<name>:<path relative to efisp/>`.
