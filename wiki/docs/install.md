# Installation Guide

## Boot Flow

The real ABL loads an embedded **superfastboot BDS** off the raw `efisp` partition (via the GBL vulnerability). The BDS then scans a compatible partition for boot entries and chains to the selected one.

On this device the boot root is the `persist` partition (ext4, auto-mounted at `/mnt/vendor/persist`), under its `efisp/` directory:

| File | Purpose |
|------|---------|
| `boot.efi` | Cracked ABL with fake re-lock (`ANDROID` entry) |
| `boot_backup.efi` | Previous `boot.efi` (`ANDROID_BACKUP` entry) |
| `BOOTENTRIES` | Boot entry list, format `<name>:<path relative to efisp/>` |

`BDS.efi` is written raw to the `efisp` partition (not into a filesystem).

## 1. Prerequisite: GBL Vulnerability

The ABL on the `abl` partition must contain the **GBL vulnerability** so it loads the BDS off `efisp`. If your ABL lacks it, flash an **older ABL version** that has the vulnerability to the `abl` partition first. The cracked `boot.efi` does **not** need to match the ABL on the `abl` partition.

## 2. Install Method

| Method | Description |
|--------|-------------|
| **KernelSU module (Recommended)** | Automated: extracts & cracks the current ABL, lays out the boot root, and flashes the BDS |
| **Toolkit (Manual)** | For traditional users: run `build.sh` / `build.bat` on your `abl.img`, then place files and flash manually |

## 3. Module Install (KernelSU)

### 3.1 Fresh install

1. Install the module via KernelSU. When prompted, press **Vol+ (YES)**.
   The module extracts & cracks the current-slot ABL, places `boot.efi` / `BOOTENTRIES` into `/mnt/vendor/persist/efisp/`, and flashes `BDS.efi` to `efisp`.
2. Reboot to **Recovery** and **format data**.
   > ⚠️ The first reboot may crash — simply retry.
3. Reinstall the module and press **Vol- (NO)** to install the OTA-update patch.
4. Reboot the system.

### 3.2 After an OTA

After each OTA, open the module WebUI and flash again to keep the BL version (re-cracks the new ABL to the inactive slot / refreshes the boot root).

## 4. Toolkit Install (Manual)

> The toolkit is manual-install only; superfb does not provide automated installation for toolkit users.

1. Place your `abl.img` in the toolkit `images/` folder and run `build.sh` (Android/Linux) or `build.bat` (Windows). Outputs:
   - `ABL.efi` — cracked ABL (fake re-lock)
   - `ABL_original.efi` — original unpatched ABL
   - `BDS.efi` — bundled
2. Create the folder `/mnt/vendor/persist/efisp` (e.g. via MT Manager).
3. Copy `ABL.efi` into it.
4. Create `BOOTENTRIES` with:
   ```
   ANDROID:ABL.efi
   ```
5. `sync`
6. Flash `BDS.efi` to the `efisp` partition:
   ```
   dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M
   ```
   If the build log shows `Failed to patch ABL GBL`, downgrade the `abl` partition to an older ABL with the vulnerability before booting.

## 5. Re-lock Mode

| Mode | Applicable scenario |
|------|---------------------|
| **True re-lock** | Devices with official unlock support (e.g. OnePlus), or international devices that passed official unlock review |
| **Fake re-lock** | Force-unlocked devices, or devices needing official fastboot as fallback |

- **Fake re-lock**: the cracked `boot.efi` already provides the fake lock.
- **True re-lock**: boot into the BDS (Super Fastboot) and perform the re-lock operation.
  - ✅ Some devices (e.g. Dami): re-lock does **not** wipe data.
  - ⚠️ OnePlus: requires a **deep test unlock**; data will be lost, but root is retained.

## ⚠️ Important Warnings

> Before performing any operation, verify the following:

- 📌 Restore any partition **other than those containing `boot`** that you modified.
- 📌 Partitions verified by `init`: **AVB must remain enabled** — do not modify.
- 📌 The `dtbo` partition verified by ABL **must not be modified** in true re-lock mode.
- ❌ **Do NOT install TWRP** — it will cause **data corruption**.
