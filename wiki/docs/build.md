# Build Guide

## Build the release packages

From the repository root, build the four supported packages with:

```bash
make target_toolkit_linux
make target_toolkit_windows
make target_toolkit_android
make target_magisk_module
```

Android and module builds require `NDK_PATH` to point to an Android NDK. Archives
are written below each `targets/toolkit_*/build/` directory and
`targets/magisk_module/build/`.

## Single-source versioning

The repository-root `version.mk` is the single source of truth and contains
exactly:

```make
CANOE_VERSION = 7.0.0-b1
CANOE_VERSION_CODE = 14
```

Run `make bump VERSION=x.y.z` to regenerate every derived file. Run
`make version-check` to fail on version drift. The UEFI build stamps the same
`CANOE_VERSION` value into `SFB_BDS_VERSION`, which is published by the BDS as
the `canoe-bds` fastboot variable.

## Host command surface

The host toolkit is a Python 3.11 package. Linux uses `./canoe`; the Windows
archive includes an embeddable interpreter and uses `canoe.cmd`.

```text
canoe
canoe build [--abl IMG] [--vbmeta IMG]
canoe install [--boot-root PATH] --slot a|b [--mode 0|1|2] \
              [--vendor-boot IMG] [--allow-new-signer]
```

`canoe` with no arguments starts the interactive five-scenario questionnaire.
`canoe build` derives the patched ABL and both sidecars. By default it reads
`images/abl.img` and `images/vbmeta.img`; `--abl` and `--vbmeta` copy supplied
files into those canonical locations before deriving. The images must match the
firmware being booted. The default path uses stock images; an explicitly
supplied Custom ROM `vbmeta` is accepted when the installer's signer policy
allows that declared change.

`canoe install` validates and commits the staged boot root for the required
active slot. Without `--boot-root`, the host reaches the boot root through the
BDS `fastboot oem mass-storage:persist` export. A provided `--boot-root` points
to an already mounted `persist/efisp` directory. `--vendor-boot IMG` creates a
patched copy for the selected slot and reports the corresponding fastboot flash;
the source image is never modified. `--allow-new-signer` permits an expected
signer change when moving to or from a custom ROM.

## Host derivation tools

The Linux and Android packages contain `extractfv`, `patch_abl`,
`mode2_profile`, and `abl_tzmap`. The Windows package contains their `.exe`
forms. `mode2_profile` provides `derive` and `validate` for the 120-byte
KeyMint profile. `abl_tzmap` derives and validates the 256-byte `GTZM` map from
the unpatched ABL and accepts incomplete reverse-engineering evidence.

The `vbmetaport` utility remains available as the standalone recovery-vbmeta
graft tool referenced by the Mode 1 questionnaire. No boot-image binary is
bundled: the host `vendor_boot` feature is a fixed-offset, in-place cmdline
amendment.

## Build a matching pair

Place the matching stock images at:

```text
images/abl.img
images/vbmeta.img
```

Then run:

```bash
./canoe build
```

The result is a patched `boot.efi`, its exact 120-byte
`boot.efi.gm2p`, and a 256-byte `boot.efi.tzmap`. The map is derived from the
unpatched ABL. The installation transaction copies all required files together
and rolls the tree back if a commit fails.

## Bootloader prerequisite

The operator owns the raw fastboot step. If the installed ABL does not contain
the GBL vulnerability, flash an older vulnerable stock image, then flash BDS:

```bash
fastboot flash abl <vulnerable>.img
fastboot flash efisp BDS.efi
```

Omit the first command when the installed ABL is already vulnerable. Never
flash `persist`; it is a live ext4 filesystem containing the boot root and
vendor data.

## Windows package

The Windows archive bundles `fastboot.exe` and `canoe-ext4.exe`, the bundled
userspace ext4 engine. No drive letter, filesystem driver, or mount is
involved: `canoe.cmd install --slot <A|B>` discovers the exported disk by its
USB identity and runs the boot-root transaction through `canoe-bootmgr.exe`
against the raw `\\.\PhysicalDrive<N>` source. To probe a disk by hand:

```text
canoe-ext4.exe inspect \\.\PhysicalDrive<N>
```

## What `patch_abl` changes

`libavb_force_success` is mandatory; patching fails without it. Other changes
are best effort because they affect functionality rather than bootability:

- lock-state fastboot gates for `flash`, `erase`, slot changes, and snapshot
  cancellation;
- the Oplus orange-state warning; and
- force-enable-fastboot behavior.

`Warning: Failed to patch ABL GBL` means the input ABL lacks the vulnerability.
The `abl` partition must then be downgraded with a compatible vulnerable image.

## Developer note

After editing UEFI sources, rebuild a target with
`UEFI_REBUILD=1 make target_<name>`, or run `make clean` first. There is no
separate generic build.
