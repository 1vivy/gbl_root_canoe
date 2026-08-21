# Installation Guide

## Boot Flow

The real ABL loads an embedded **superfastboot BDS** off the raw `efisp` partition (via the GBL vulnerability). The BDS then scans a compatible partition for boot entries and chains to the selected one.

On this device the boot root is the `persist` partition (ext4, auto-mounted at `/mnt/vendor/persist`), under its `efisp/` directory:

| File | Purpose |
|------|---------|
| `boot.efi` | Patched ABL launched by the `ANDROID` entry |
| `boot.efi.gm2p` | Matching 120-byte locked/green KeyMint profile derived from the matching stock vbmeta |
| `boot.efi.tzmap` | Optional 256-byte `GTZM` ABL-derived TrustZone interface map from the unpatched ABL |
| `boot_backup.efi` / `.gm2p` / `.tzmap` | Previous complete ABL/profile/map set |
| `BOOTENTRIES` and `tools/` | Boot entry list and tools submenu |

`BDS.efi` is written raw to the `efisp` partition (not into a filesystem).

## 1. Prerequisite: GBL Vulnerability

The ABL on the `abl` partition must contain the **GBL vulnerability** so it loads the BDS off `efisp`. If your ABL lacks it, flash an **older ABL version** that has the vulnerability to the `abl` partition first. The patched `boot.efi` and its sidecars may differ from that downgraded partition ABL: derive `boot.efi.gm2p` from the matching stock vbmeta, and derive the optional `boot.efi.tzmap` from the **unpatched ABL that produced `boot.efi`**.

## 2. Install Method

| Method | Description |
|--------|-------------|
| **KernelSU module (Recommended)** | Automated: patches the current ABL, derives its matching profile from current-slot vbmeta, derives the optional map from the unpatched ABL, lays out the boot root, and flashes the BDS |
| **Toolkit (Manual)** | Run `build.sh` / `build.bat` with matching `abl.img` and `vbmeta.img`, then place the generated tree and flash manually |

## 3. Module Install (KernelSU)

### 3.1 Fresh install

1. Install the module via KernelSU. When prompted, press **Vol+ (YES)**.
   The module patches the current-slot ABL, derives `boot.efi.gm2p` from matching current-slot vbmeta, generates the optional `boot.efi.tzmap` locally from the unpatched ABL, installs the complete pair plus map, `BOOTENTRIES` and tools under `/mnt/vendor/persist/efisp/`, and flashes `BDS.efi` to `efisp`.
2. Reboot to **Recovery** and **format data**.
   > ⚠️ The first reboot may crash — simply retry.
3. Reinstall the module and press **Vol- (NO)**. This skips boot-chain writes and installs only the module/WebUI used for later OTA retention.
4. Reboot the system.

### 3.2 After an OTA

After each OTA, open the module WebUI and flash again to retain the BL version. Keep **Update efisp** enabled when refreshing the patched ABL/profile/map set; the installer regenerates `.gm2p` from matching stock vbmeta and `.tzmap` from the unpatched ABL.

## 4. Toolkit Install (Manual)

> The toolkit is manual-install only; superfb does not provide automated installation for toolkit users.

1. Place matching stock `abl.img` and `vbmeta.img` files in the toolkit `images/` folder and run `build.sh` (Android/Linux) or `build.bat` (Windows). Outputs include:
   - `efisp/boot.efi` — patched ABL
   - `efisp/boot.efi.gm2p` — matching 120-byte profile derived from the matching stock vbmeta
   - `efisp/boot.efi.tzmap` — optional local 256-byte `GTZM` TrustZone map derived from the unpatched ABL
   - `efisp/BOOTENTRIES` and `efisp/tools/` — boot menu tree
   - `ABL_original.efi` — extracted original for analysis; do not flash it
   - `BDS.efi` — bundled BDS image
   The `.tzmap` is generated locally and is not shipped inside the toolkit archive.
2. Create `/mnt/vendor/persist/efisp` if needed.
3. Copy the complete generated `efisp/` tree into it:
   ```
   cp -r efisp/. /mnt/vendor/persist/efisp/
   ```
4. `sync`
5. Flash `BDS.efi` to the `efisp` partition:
   ```
   dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M
   ```
   If the build log shows `Failed to patch ABL GBL`, downgrade the `abl` partition to an older ABL with the vulnerability before booting.

## 5. Preferred Boot Mode

| Mode | Behavior |
|------|----------|
| **Mode 0 — Honest unlocked** | Pass-through ABL/TrustZone behavior; universal SCM fuse and anti-rollback drops still apply on a best-effort basis |
| **Mode 1 — ABL fake locked** | Projects locked DeviceInfo to ABL and KeyMaster `READ_DEVICE_STATE`, suppresses `WRITE_DEVICE_STATE`, and retains the universal SCM drops |
| **Mode 2 — TrustZone-only** | Rewrites matching KeyMaster/TrustZone requests from `boot.efi.gm2p`; the ABL-facing state legitimately remains orange/unlocked, and universal SCM drops still apply |

### Universal SCM safeguards (all modes)

All modes (0/1/2) best-effort suppress the TrustZone fuse request (`0x02000801`) and anti-rollback SCM requests (`0x0200011E` and `0x32000110`). This prevents **further advancement only**: it cannot un-blow an already-blown fuse or lower an already-raised rollback floor. If the SCM protocol is absent, launch continues and the `hooks-armed ... scm=0` marker records that the safeguard was unavailable.

Choose the preferred mode in the BDS menu or module WebUI. The choice is stored in the fixed tail record on `efisp`; a missing or malformed record defaults to Mode 1. Mode 2 requires the matching 120-byte `.gm2p` profile; if that profile is missing or invalid, launch falls back to Mode 0. The 256-byte `.tzmap` is optional; if it is missing or invalid, BDS uses its built-in fallback.

Hardware bootloader re-locking is a separate operation. Use only a device-supported flow and account for vendor-specific data-wipe requirements.

## ⚠️ Important Warnings

> Before performing any operation, verify the following:

- 📌 Restore any partition **other than those containing `boot`** that you modified.
- 📌 Partitions verified by `init`: **AVB must remain enabled** — do not modify.
- 📌 The `dtbo` partition verified by ABL **must not be modified** while the bootloader is hardware re-locked.
- ❌ **Do NOT install TWRP** — it will cause **data corruption**.
