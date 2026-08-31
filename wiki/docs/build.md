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
CANOE_VERSION = 7.0.0-b2
CANOE_VERSION_CODE = 14
```

Run `make bump VERSION=x.y.z` to regenerate every derived file. Run
`make version-check` to fail on version drift. The UEFI build stamps the same
`CANOE_VERSION` value into `SFB_BDS_VERSION`, which is published by the BDS as
the `canoe-bds` fastboot variable.

## Host command surface

The host toolkit ships a native `canoe` binary at the archive root (`canoe.exe`
on Windows), alongside `canoe-gui` and `bin/canoe-bootmgr`. No Python
installation or bundled interpreter is required.

```text
canoe
canoe build [--abl IMG] [--vbmeta IMG]
canoe install [--boot-root PATH] --slot A|B [--mode 0|1|2] \
              [--vendor-boot IMG] [--allow-new-signer]
canoe entry|config|default|bls|slot|source ...
canoe -h | --help | --version
canoe --non-interactive <command> ...
```

With no arguments, `canoe` starts the interactive five-scenario questionnaire.
`--non-interactive` is accepted and discarded for compatibility. The
`entry|config|default|bls|slot|source` verbs are forwarded verbatim to
`canoe-bootmgr`.

Build the native host binary with:

```bash
cargo build --locked --release --manifest-path tools/canoe/Cargo.toml
```

The crate requires Rust `rust-version = 1.85`. Use the rustup toolchain when
cross-building the Windows target:

```bash
cargo build --locked --release --target x86_64-pc-windows-gnu \
  --manifest-path tools/canoe/Cargo.toml
```

## `canoe-bootmgr build`

`canoe-bootmgr build` is the single payload-derivation orchestrator used by
host and device callers. A full build is:

```text
canoe-bootmgr build --abl <ABL_IMAGE> --vbmeta <VBMETA_IMAGE> --staged <DIR> \
                    [--tools <DIR>] [--keep-unpatched <PATH>] [--patch-log <PATH>]
```

It extracts the ABL into a work directory, runs
`extractfv -o <workdir> -v <abl>`, and requires
`<workdir>/LinuxLoader.efi`. It then runs
`patch_abl <workdir>/LinuxLoader.efi <staged>/boot.efi`, requiring a
non-empty output, followed by `mode2_profile derive --vbmeta <vbmeta> --out
<staged>/boot.efi.gm2p` and its `validate` command. Finally it runs
`abl_tzmap derive <workdir>/LinuxLoader.efi -o <staged>/boot.efi.tzmap
--allow-incomplete`, then validates and verifies that sidecar against the
extracted loader with `--allow-zero-digest`. The profile must be exactly 120
bytes and the map exactly 256 bytes. On success, `<staged>` contains exactly
`boot.efi`, `boot.efi.gm2p`, and `boot.efi.tzmap`.

The four worker binaries remain separate: `extractfv`, `patch_abl`,
`mode2_profile`, and `abl_tzmap`. `--keep-unpatched` copies the extracted
loader, and `--patch-log` records the captured `patch_abl` output. A
`Warning: Failed to patch ABL GBL` line is not a build error: the receipt
reports `gbl_patched: false`, and the sidecars describe the stock pair.

For a side-effect-free worker probe (no vbmeta, sidecars, or staged outputs),
use:

```text
canoe-bootmgr build --abl <ABL_IMAGE> --probe [--tools <DIR>]
```

Tools resolve in this order: `--tools <DIR>`, `$CANOE_TOOLS_DIR`, the directory
containing the running `canoe-bootmgr`, then `PATH`. A missing tool is an
error naming that tool. Any failure removes all three staged outputs and any
`--keep-unpatched` or `--patch-log` file created by that invocation, so a
fresh loader can never remain beside a stale sidecar.

`canoe build` is the host convenience surface for this same orchestrator; it
does not maintain a separate derivation implementation.


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

The Windows archive bundles the native `canoe.exe`, `fastboot.exe`, and
`canoe-ext4.exe`, the bundled userspace ext4 engine. No Python installation or
bundled interpreter is needed, and no launcher script is used. No drive letter,
filesystem driver, or mount is involved: `canoe.exe install --slot <A|B>` asks
`canoe-bootmgr source detect --json` for the exported source and runs the
boot-root transaction through `canoe-bootmgr.exe` against the raw
`\\.\PhysicalDrive<N>` source. To probe a disk by hand:

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

## GUI and archive layout

The Linux archive contains `bin/canoe-gui` (release build) and a root-level
`canoe-gui` launcher. Open the root `canoe-gui` from a file manager or run it
from any current directory; the launcher supplies the bundled boot manager path.
The Windows archive places the no-console `canoe-gui.exe` at its root; helper
binaries remain in `bin/`. Android and Magisk archives contain no GUI.

The GUI Connect screen runs `source detect`, offers attach, Refresh, and manual
directory/image/device selection, and remembers the last successful source in
the platform config directory. Elevation is not needed for directory or image
sources. Access-denied device operations show an explicit Linux `pkexec`/sudo
retry or Windows **Restart as Administrator** action.

The Windows helper supports explicit dirty-journal recovery with
`canoe-ext4.exe --recover`; code 4 reports a dirty filesystem, and recovery is
never implicit.

## Device-series artifact provenance
The device-series Linux artifacts are maintained outside this repository. The
current provenance is `FantomTchi7/kaanapali-mainline-linux`, branch
`OnePlus-15-WIP`, commit `2d1ab8738563b8771e18b5939f00bb3361dd873a2` (2026-04-22).
The board DTS is
`arch/arm64/boot/dts/qcom/kaanapali-oneplus-infiniti.dts`; build its DTB with
`make ARCH=arm64 ... arch/arm64/boot/dts/qcom/kaanapali-oneplus-infiniti.dtb`.
It declares `compatible = "oneplus,infiniti"` and `dr_mode = "peripheral"`;
there is no `stdout-path`, and `uart7`/`uart18` are disabled. The arm64
defconfig materializes `EFI=y` and `EFI_STUB=y`; use an uncompressed `Image`.
H3 BLS paths under `persist` are `\\efisp\\vmlinuz-canoe`,
`\\efisp\\initramfs-canoe`, and
`\\efisp\\dtbs\\kaanapali-oneplus-infiniti.dtb`. The marker endpoint is
`telnet 192.168.42.1:2323`. Full provenance and the preparation script remain
under `.work/device-series`; they are not repository source files.
