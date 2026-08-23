# Installation Guide

## Boot Flow

The real ABL loads an embedded **superfastboot BDS** off the raw `efisp` partition (via the GBL vulnerability). The BDS then scans a compatible partition for boot entries and chains to the selected one.

On this device the boot root is the `persist` partition (ext4), under its `efisp/` directory. A booted Android system auto-mounts it at `/mnt/vendor/persist`; a custom recovery mounts it at `/persist`. The BDS itself does not care which: it scans every ext4 volume for an `efisp/` directory and treats that as the boot root.

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
| **KernelSU module (Recommended)** | Automated, from a booted rooted system: patches the current ABL, derives its matching profile from current-slot vbmeta, derives the optional map from the unpatched ABL, lays out the boot root, and flashes the BDS |
| **Toolkit, standalone** (§4.1) | Host-driven over ADB from a custom recovery. Needs no firmware package, no graft, and no root on the running system |
| **Toolkit, with a firmware package** (§4.2) | For the Super Flasher / RegionalHybrid workflow: prepare the package's inputs, run the package's own flasher unchanged, then stage |
| **Toolkit, fully manual** (§4.3) | Run `build.sh` / `build.bat`, then place the tree and flash the BDS by hand |

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

## 4. Toolkit Install

`build.sh` / `build.bat` only *derives* artifacts. Placing them is done by one of the three flows below. All of them produce the same on-device result; they differ only in where the inputs come from and who writes the partitions. Both the Linux and Windows toolkits ship the same scripts — Linux as `.sh`, Windows as `.bat`, with identical options and behaviour — so the commands below are written once and apply to either.

Outputs of a `build.sh` run:

- `efisp/boot.efi` — patched ABL
- `efisp/boot.efi.gm2p` — matching 120-byte profile derived from the matching stock vbmeta
- `efisp/boot.efi.tzmap` — optional local 256-byte `GTZM` TrustZone map derived from the unpatched ABL
- `efisp/BOOTENTRIES` and `efisp/tools/` — boot menu tree
- `ABL_original.efi` — extracted original for analysis; do not flash it
- `BDS.efi` — bundled BDS image

The `.tzmap` is generated locally and is not shipped inside the toolkit archive.

### 4.1 Standalone (custom recovery + ADB)

The only prerequisite is a custom recovery with ADB enabled. Persist is writable there, so no root on the running system is needed.

```bash
./canoe_prep_device.sh     # pull abl + vbmeta, derive boot.efi and its sidecars
./canoe_stage.sh           # install the persist tree, then write the BDS
```

The pair is pulled from the **active** slot by default. When installing right after an `adb sideload` — the usual custom-ROM flow — the sideload has written the *other* slot and not booted it yet, so pass `--slot inactive` to derive from that slot instead:

```bash
./canoe_prep_device.sh --slot inactive
```

Then, only if the `abl` partition is not already a GBL-vulnerable version:

```bash
fastboot flash abl <vulnerable>.img
```

**Order matters.** `boot.efi` is derived from `abl` and `boot.efi.gm2p` from `vbmeta`, and the two must describe the *same* firmware. Pulling both from the device yields a matching pair only while `abl` still holds its original image, so run `canoe_prep_device.sh` **before** downgrading. If the partition was already downgraded, supply a matching stock pair instead:

```bash
./canoe_prep_device.sh --abl stock_abl.img --vbmeta stock_vbmeta.img
```

Both flags are required together; accepting one alone would reintroduce exactly the version mismatch they guard against. `canoe_prep_device.sh` reports whether the source ABL carried the vulnerability, so its output tells you whether the `fastboot flash abl` step is needed.

### 4.2 Alongside a firmware package

For the Super Flasher / RegionalHybrid workflow. This flow does **not** reimplement the packaged flasher: it prepares correct inputs, and the package's own script then runs unmodified. Slot selection, `--slot=all` loops and logical-partition handling remain the flasher's business.

```bash
# 1. host side, no device needed
./canoe_prep.sh --pkg OOS_FILES_HERE \
                --recovery <custom>.img \
                --abl <vulnerable>.img \
                --in-place

# 2. run the package's own flasher, unchanged
bash Super_Flasher.sh

# 3. boot the custom recovery, enable ADB
./canoe_stage.sh
```

`--in-place` substitutes the prepared images into the package directory, keeping `<name>.img.canoe-orig` backups; rerunning never overwrites an existing backup with an already-substituted image.

Because the flasher writes the package's own `recovery.img` to both slots, keeping a custom recovery means the image it writes has to be the custom one — which is why this flow has a graft step and §4.1 does not. `canoe_prep.sh` lifts the official recovery vbmeta out of the package's `recovery.img` with `vbmetabackup -f` (host-side, no device) and transplants it onto the custom recovery with `vbmetaport`, preserving the partition size and the custom payload.

`--abl` only changes which ABL image the flasher writes to the `abl` partition. The sidecars are always derived from the package's **stock** `abl.img` + `vbmeta.img` pair, because that is the pair `boot.efi` and `boot.efi.gm2p` must agree with.

### 4.3 Fully manual

1. Place matching stock `abl.img` and `vbmeta.img` in the toolkit `images/` folder and run `build.sh` (Android/Linux) or `build.bat` (Windows).
2. Create the boot root if needed — `/mnt/vendor/persist/efisp` from a booted system, `/persist/efisp` from recovery.
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

### What the staging step guarantees

`canoe_stage` is shared by §4.1 and §4.2, and is a thin driver: it validates and stages, then hands the transaction to `canoe_device_install.sh` running on the device. That single device-side script is the only implementation of the transaction, so the Linux and Windows drivers cannot drift apart.

- The staged set is pushed and validated before anything live is touched, so a failed transfer changes nothing.
- Everything the commit overwrites is snapshotted first: the live triplet, the existing backup generation, `BOOTENTRIES` and `tools/`. A rollback therefore never leaves one generation's loader beside another's menu tree.
- The previous generation is demoted to `boot_backup.efi` plus matching sidecars — a managed path the BDS recognises and an entry the shipped `BOOTENTRIES` already lists, so it stays selectable from the boot menu.
- The persist tree is complete and synced **before** the BDS is written, so an interrupted run never leaves a live BDS pointing at half-installed sidecars.
- A failed first install leaves no partial `boot.efi` behind.
- The BDS write is preceded by a full backup of `efisp` and followed by a byte-for-byte comparison of the written region; either failing restores the partition. The backup is pulled to the host either way.
- The preferred-mode record is left untouched unless `--mode N` is passed (which sets the preferred boot mode after a successful install, verified by reread); the `abl` partition is never touched.

## 5. Preferred Boot Mode

| Mode | Behavior |
|------|----------|
| **Mode 0 — Honest unlocked** | Pass-through ABL/TrustZone behavior; universal SCM fuse and anti-rollback drops still apply on a best-effort basis |
| **Mode 1 — ABL fake locked** | Projects locked DeviceInfo to ABL and KeyMaster `READ_DEVICE_STATE`, suppresses `WRITE_DEVICE_STATE`, and retains the universal SCM drops |
| **Mode 2 — TrustZone-only** | Rewrites matching KeyMaster/TrustZone requests from `boot.efi.gm2p`; the ABL-facing state legitimately remains orange/unlocked, and universal SCM drops still apply |

### Universal SCM safeguards (all modes)

All modes (0/1/2) best-effort suppress the TrustZone fuse request (`0x02000801`) and anti-rollback SCM requests (`0x0200011E` and `0x32000110`). This prevents **further advancement only**: it cannot un-blow an already-blown fuse or lower an already-raised rollback floor. If the SCM protocol is absent, launch continues and the `hooks-armed ... scm=0` marker records that the safeguard was unavailable.

### Universal reserve-token safeguard (all modes)

All modes (0/1/2) swallow writes to a vendor reserve partition that carries the fastboot unlock token (`oplusreserve1`, or its legacy `opporeserve1` name) for as long as the chainloaded ABL is running. The vendor relock path zeroes that token block, and the loss is one-way: once zeroed, the device can no longer be unlocked from fastboot. The write is reported as successful to the ABL so its state machine still completes.

This is not device-specific. A platform carrying no such partition arms nothing, launch continues, and the `hooks-armed ... reserve=0` marker records that the safeguard did not apply. Superfastboot's own `fastboot flash oplusreserve1` is unaffected, because the slot is only wrapped across a managed ABL launch.

The reserve partition has many routine writers (Phoenix boot accounting, charge/UFS state), all of which are swallowed silently and logged. The one destructive write — zeroing the token block at `LastBlock - 0x3A5` — is additionally announced on screen, once per launch:

`SFB: blocked unlock-token erase on oplusreserve1 LBA 1114; token preserved`

Every swallow is recorded in `UefiLog<N>.txt` on the `logfs` partition with a `reason=` field (`token-zero-write`, `token-block-write`, `unlock-record-write`, `reserve-write`). `DEBUG` output never reaches the framebuffer, so that log is the only place the routine swallows appear.

Choose the preferred mode in the BDS menu, the module WebUI, or at install time via `canoe_stage --mode N`. The choice is stored in the fixed tail record on `efisp`; a missing or malformed record defaults to Mode 1. Mode 2 requires the matching 120-byte `.gm2p` profile; if that profile is missing or invalid, launch falls back to Mode 0. The 256-byte `.tzmap` is optional; if it is missing or invalid, BDS uses its built-in fallback.

Hardware bootloader re-locking is a separate operation. Use only a device-supported flow and account for vendor-specific data-wipe requirements.

## ⚠️ Important Warnings

> Before performing any operation, verify the following:

- 📌 Restore any partition **other than those containing `boot`** that you modified.
- 📌 Partitions verified by `init`: **AVB must remain enabled** — do not modify.
- 📌 The `dtbo` partition verified by ABL **must not be modified** while the bootloader is hardware re-locked.
- ❌ **Do NOT install TWRP** — it will cause **data corruption**.
