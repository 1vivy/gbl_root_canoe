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

## Build an ABL/profile/map pair

Extract the toolkit for the host platform, then place the matching stock images
at:

```text
images/abl.img
images/vbmeta.img
```

Run `build.sh` on Linux/Android or `build.bat` on Windows. The script patches
the ABL, derives `efisp/boot.efi.gm2p` (the exact 120-byte KeyMint profile)
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

## Developer note

After editing UEFI sources, rebuild the BDS with `UEFI_REBUILD=1 make
target_<name>`, or run `make clean` first.

There is no separate generic build.

