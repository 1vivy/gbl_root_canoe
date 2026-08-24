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

The Linux and Android toolkits contain `extractfv`, `patch_abl`, `mode2_profile`, and `abl_tzmap`. The Windows toolkit contains `extractfv.exe`, `patch_abl.exe`, `mode2_profile.exe`, and `abl_tzmap.exe`. `abl_tzmap` derives and validates the local 256-byte `GTZM` `boot.efi.tzmap` TrustZone interface map from the **unpatched ABL**.

The Linux toolkit additionally ships `vbmetaport` and `vbmetabackup` from `tools/vbmetafixer`, and the Windows toolkit their `.exe` builds, which the host-side install scripts use to graft an official vbmeta onto a custom recovery image. `vbmetabackup -f <image>` performs that extraction from a local firmware image, with no device and no adb; without `-f` it keeps its original behaviour of pulling the live chain over adb.

## Build an ABL/profile/map pair

Extract the toolkit for the host platform, then place the matching stock images
at:

```text
images/abl.img
images/vbmeta.img
```

Run `./canoe_build` on the Linux toolkit or `canoe_build.cmd` on the Windows
toolkit. The Android toolkit retains its `build.sh`: it runs on-device, where
`python3` is not guaranteed. The host implementation patches the ABL, derives
`efisp/boot.efi.gm2p` (the exact 120-byte KeyMint profile)
from the matching root vbmeta image, and generates the local
`efisp/boot.efi.tzmap` (the 256-byte `GTZM` ABL-derived TrustZone map) from the
unpatched ABL. The `.tzmap` is stored beside the launched image as
`/mnt/vendor/persist/efisp/boot.efi.tzmap` and is not shipped inside the
toolkit archive. Both `efisp/boot.efi` and its exact 120-byte `.gm2p` sidecar
must be installed together; the `.tzmap` is optional at runtime because BDS
has a built-in fallback.

The build scripts pass `--allow-incomplete` to `abl_tzmap`, so an ABL with no
recorded reverse-engineering evidence still receives a valid 256-byte sidecar
with its identifier flags and protocol command table. Installation does not
fail for that reason.

## Host-side install tools

The host drivers are now one Python implementation copied into both archives.
This replaces two separately maintained drivers; the Windows one kept breaking
on `cmd.exe` parsing details rather than on installation logic. Linux requires
Python 3.11+ on the host; Windows needs no installation because its archive
includes an embeddable CPython under `python/`.
The source copy is `tools/canoe-host/` in the repository; each toolkit archive
ships that same package under `canoe/`.

Both toolkits ship these host launchers with identical options:

| Linux | Windows | Role |
|-------|---------|------|
| `canoe_prep_device` | `canoe_prep_device.cmd` | Standalone preparation: pull `abl` + `vbmeta` and derive the triplet |
| `canoe_prep` | `canoe_prep.cmd` | Firmware-package preparation: graft a custom recovery and substitute prepared images |
| `canoe_stage` | `canoe_stage.cmd` | Host driver: validate, stage into the boot root, invoke the device-side transaction |

They are documented in `README.canoe.md` inside each archive and in the
Installation Guide. `canoe_device_install.sh` is deliberately still a shell
script: it is the single install transaction executed on the device, where
Python is not guaranteed.

`canoe_device_install.sh` lives in `tools/canoe-device/` and is copied into
both toolkits by their `canoe_device_script` make target. The snapshot,
commit and rollback logic therefore has exactly one implementation. Every
absolute device path arrives as an argument, which also lets it be tested
directly on a host.

None of the host tools touch the `abl` partition: making that partition carry
the GBL vulnerability is a separate `fastboot flash abl` step.

Fixture coverage:

- `targets/toolkit_linux/tests/test_canoe_device_install.sh` drives the
  transaction natively against ordinary directories and a file standing in
  for the block device, injecting commit, write and verification failures by
  shadowing `mv`, `dd` and `cmp`.
- `targets/toolkit_linux/tests/test_canoe_scripts.sh` drives both preparation
  pathways and the staging driver against `tests/stub_adb.py`.

Both are registered in `make test`.

## What `patch_abl` rewrites

`libavb_force_success` is mandatory — patching fails outright without it. The rest are best effort and only warn, because losing them costs functionality rather than bootability:

- **Lock-state fastboot gates.** The ABL is handed a locked view, so its in-fastboot dispatcher would otherwise refuse `flash`, `erase`, slot change and snapshot cancel. Each refusal message is anchored to the gate guarding it, and the gate is either made unconditional or removed. All-or-nothing: if any gate present in the image cannot be resolved, nothing is written, because an ABL that accepts `flash` but still refuses `erase` is worse than one that refuses both. `patch_log.txt` names each rewritten offset.
- **Oplus orange-state warning** and **force-enable-fastboot** — cosmetic and usability, Oplus-specific.

A `Warning: Failed to patch ABL GBL` line means the ABL lacks the vulnerability and the `abl` partition must be downgraded.

## Developer note

After editing UEFI sources, rebuild the BDS with `UEFI_REBUILD=1 make
target_<name>`, or run `make clean` first.

There is no separate generic build.

