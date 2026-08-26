# Build Guide

## Build the packages

From the repository root, build the platform packages with:

```bash
make target_toolkit_linux
make target_toolkit_windows
make target_toolkit_android
make target_magisk_module
```

Android and module builds require `NDK_PATH` to point to the Android NDK. The
toolkit archives are written below each `targets/toolkit_*/build/` directory;
the module archive is written below `targets/magisk_module/build/`.

## Toolkit utilities

The Linux and Android toolkits contain `extractfv`, `patch_abl`, `mode2_profile`, and `abl_tzmap`. The Windows toolkit contains `extractfv.exe`, `patch_abl.exe`, `mode2_profile.exe`, and `abl_tzmap.exe`. `mode2_profile` exposes only `derive` and `validate`; it does not write a persistent mode. `abl_tzmap` derives and validates the local 256-byte `GTZM` `boot.efi.tzmap` TrustZone interface map from the **unpatched ABL**.

The Linux toolkit additionally ships `vbmetaport` and `vbmetabackup` from `tools/vbmetafixer`, and the Windows toolkit their `.exe` builds. The host-side package flow uses these to graft an official vbmeta onto a custom recovery image. `vbmetabackup -f <image>` performs that extraction from a local firmware image, with no device and no ADB; without `-f` it keeps its original behaviour of pulling the live chain over ADB.

## Host entry point

The host implementation is the `canoelib/` Python package, copied into both toolkit archives. Linux requires Python 3.11+; the Windows archive includes an embeddable CPython under `python/`. Each archive has one entry point:

```text
Linux:   ./canoe
Windows: canoe.cmd
```

With no arguments it runs the interactive wizard. The wizard asks whether this is a first install or update, which mode to use, the Mode 1 recovery-graft and `vendor_boot` choices, waits for matching **stock** `images/abl.img` and `images/vbmeta.img` if necessary, asks whether to generate a boot entry, and prints a readable result.

The scriptable surface is:

```text
canoe                              interactive wizard (default)
canoe build                        derive the ABL/profile/map artifacts
canoe prep [--pkg ...]             prepare a firmware package
canoe prep-device [--slot ...]     pull a device pair and derive artifacts
canoe install [--skip-bds ...]     install the boot root over ADB
canoe oneshot --abl <img> --mode 0|1
                                   temporary, non-interactive boot
```

Use `canoe.cmd` in place of `canoe` on Windows. Existing options carry over unchanged under their new verb. The candidate images must be stock and must match the firmware version being booted.

## Build an ABL/profile/map pair

Extract the toolkit for the host platform, then place the matching stock images
at:

```text
images/abl.img
images/vbmeta.img
```

Run `canoe build` on Linux or `canoe.cmd build` on Windows. The Android toolkit retains its `build.sh`: it runs on-device, where `python3` is not guaranteed. The host implementation patches the ABL, derives `efisp/boot.efi.gm2p` (the exact 120-byte KeyMint profile) from the matching root vbmeta image, and generates the local `efisp/boot.efi.tzmap` (the 256-byte `GTZM` ABL-derived TrustZone map) from the unpatched ABL. The `.tzmap` is stored beside the launched image as `/mnt/vendor/persist/efisp/boot.efi.tzmap` and is not shipped inside the toolkit archive. Both `efisp/boot.efi` and its exact 120-byte `.gm2p` sidecar must be installed together; the `.tzmap` is optional at runtime because BDS has a built-in fallback.

The build scripts pass `--allow-incomplete` to `abl_tzmap`, so an ABL with no recorded reverse-engineering evidence still receives a valid 256-byte sidecar with its identifier flags and protocol command table. Installation does not fail for that reason.

## Host-side install commands

The two installation pathways remain independent:

- **Standalone custom recovery + ADB:** `canoe prep-device [--slot ...]` pulls the ABL/vbmeta pair and derives the artifacts; after booting custom recovery, `canoe install` stages the boot root. Use `--slot inactive` when the other slot is the one just written by `adb sideload`.
- **Firmware package:** `canoe prep --pkg <dir> --recovery <custom>.img --abl <vulnerable>.img --in-place` grafts the package's official recovery vbmeta onto the custom recovery and substitutes the prepared images. Run the package's own flasher unchanged, then use `canoe install` from custom recovery.

The Windows forms prefix the same subcommands with `canoe.cmd`. `canoe install --skip-bds` installs the persist tree without writing the BDS. The host driver hands the transaction to `canoe_device_install.sh`, which remains a shell script because Python is not guaranteed in recovery/Android.

The host tools do not touch the `abl` partition: making that partition carry the GBL vulnerability is a separate `fastboot flash abl` step.

## One-shot

`canoe oneshot --abl <img> --mode 0|1` is non-interactive and intended for a locked-bootloader temporary-root launch. The input image must already be known to be stock and to match the device. It writes nothing permanent.

## Windows ext4 access

The Windows archive bundles `platform-tools`. For ext4 read/write it uses **WinFsp plus LKL `lklfuse`**, fetched and SHA-256-verified on first use rather than vendored into the repository. This enables mounting `persist` after the BDS exports it over USB Mass Storage and editing the boot root directly when ADB is unavailable.

## What `patch_abl` rewrites

`libavb_force_success` is mandatory — patching fails outright without it. The rest are best effort and only warn, because losing them costs functionality rather than bootability:

- **Lock-state fastboot gates.** The ABL is handed a locked view, so its in-fastboot dispatcher would otherwise refuse `flash`, `erase`, slot change and snapshot cancel. Each refusal message is anchored to the gate guarding it, and the gate is either made unconditional or removed. All-or-nothing: if any gate present in the image cannot be resolved, nothing is written, because an ABL that accepts `flash` but still refuses `erase` is worse than one that refuses both. `patch_log.txt` names each rewritten offset.
- **Oplus orange-state warning** and **force-enable-fastboot** — cosmetic and usability, Oplus-specific.

A `Warning: Failed to patch ABL GBL` line means the ABL lacks the vulnerability and the `abl` partition must be downgraded.

## Developer note

After editing UEFI sources, rebuild the BDS with `UEFI_REBUILD=1 make
target_<name>`, or run `make clean` first.

There is no separate generic build.
