# Build Guide

## Two repositories, one release

A Canoe release is built from two repositories. The firmware repository contains
BDS, the boot manager, workers, host CLI, and package recipes. The separate
`canoe-boot-manager` repository contains the Svelte 5 + Vite application. Build
the app checkout first; its one static `dist/` is consumed twice:

- the Tauri desktop shell packages the same app for Linux or Windows; and
- the KernelSU module serves the same files as its Android WebUI.

The app speaks the JSON wire protocol. `canoe-bootmgr` remains the only writer
of the boot root, and the Tauri shell starts it as a sidecar.

### Build the app repository

From `/path/to/canoe-boot-manager`:

```bash
bun install --frozen-lockfile
bun run typecheck
bun run build
bun test
```

The commands above are the app repository's actual package scripts. `build`
runs Vite and the asset guard; `test` also runs the asset guard before the
Bun tests. A successful build produces `dist/index.html` and the bundled
assets. The relative asset paths are intentional: the same `dist/` must load
from both a Tauri WebView and a KernelSU `file://` WebUI.

The desktop binary is built separately with Tauri. Before that compile, stage a
real target-compatible `canoe-bootmgr` sidecar in the app checkout's
`src-tauri/binaries/` directory. The required names are
`canoe-bootmgr-x86_64-unknown-linux-gnu` for Linux,
`canoe-bootmgr-x86_64-pc-windows-gnu.exe` for the tested Windows GNU route,
and `canoe-bootmgr-x86_64-pc-windows-msvc.exe` for an MSVC build. Do not use a
placeholder sidecar: Tauri validates it before compiling.

Build the desktop application from the app checkout:

```bash
# Linux
bunx tauri build --no-bundle --ci

# Windows GNU cross-build
bunx tauri build --target x86_64-pc-windows-gnu --no-bundle --ci
```

The Windows GNU command is verified on the release build host and produces a
PE32+ executable. WebView2 runtime behavior on real Windows is not proven by a
Linux cross-build; test the resulting application on Windows before publishing.
The MSVC route requires `cargo-xwin` and a real MSVC-compatible environment.

On a tag, the app repository publishes a deterministic
`canoe-boot-manager-<version>.tar.gz` containing `dist/`, plus the Linux and
Windows desktop binaries. The firmware repository consumes those assets by URL
and SHA-256 through its existing `fetch-verified` target. Until a published app
asset is selected, the checked-in
`targets/magisk_module/webui-cache/canoe-boot-manager-<CANOE_WEBUI_VERSION>.tar.gz`
is the last-known-good fallback. `make version-check` verifies that the fallback
bytes match `CANOE_WEBUI_SHA256`; it must not be silently replaced with an
unrelated bundle.

## Build prerequisites

A working release environment has all of the following:

- Docker, for the canonical EDK2/BDS build;
- the Android NDK, with `NDK_PATH` (or `ANDROID_NDK_LATEST_HOME`) set, for the
  Android toolkit and KernelSU module;
- `mingw-w64`, including `x86_64-w64-mingw32-gcc`, for Windows helpers and the
  Windows GNU builds;
- a rustup-managed Rust toolchain, including the
  `aarch64-linux-android` standard library. A distro `cargo` shim without that
  rustup target fails with `can't find crate for std`. The Windows GNU target
  is also needed for Windows Rust helpers;
- an e2fsprogs source tree and zlib headers/library for `canoe-ext4.exe`; and
- Bun for the app repository.

Check the Rust targets with the rustup toolchain's `rustup target list --installed`.
The Rust crates currently require Rust 1.85 or newer. The NDK builds
specifically use the NDK's `aarch64-linux-android31-clang` linker.

## Build the release packages

From the firmware repository root, after the app `dist/` and required desktop
binary have been built, build the four supported packages with these root
Makefile targets:

```bash
make target_toolkit_linux
make target_toolkit_windows
make target_toolkit_android
make target_magisk_module
```

Android and module builds require `NDK_PATH` to point to an Android NDK.
Archives are written below each `targets/toolkit_*/build/` directory and
`targets/magisk_module/build/`.

The Linux and Windows package recipes accept absolute app-binary overrides:

```bash
CANOE_APP_LINUX_BIN=/absolute/path/to/canoe-boot-manager/src-tauri/target/release/canoe-boot-manager \
  make target_toolkit_linux
CANOE_APP_WINDOWS_BIN=/absolute/path/to/canoe-boot-manager/src-tauri/target/x86_64-pc-windows-gnu/release/canoe-boot-manager.exe \
  make target_toolkit_windows
```

Without an override, each recipe looks for a sibling checkout of
`canoe-boot-manager`. A linked firmware git worktree is not at the repository
root that this default assumes, so a worktree build must pass the explicit
absolute `CANOE_APP_LINUX_BIN` or `CANOE_APP_WINDOWS_BIN` path.

The app binary is copied to `bin/canoe-boot-manager` (or `.exe`) next to
`bin/canoe-bootmgr`. The launchers in the toolkit root resolve that adjacency:
`canoe-boot-manager.sh` on Linux and `canoe-boot-manager.bat` on Windows.

The standalone WebUI archive is pinned independently of the desktop binary.
The module's package recipe invokes the root `fetch-verified` target, verifies
its SHA-256 before and after fetching, and extracts the app's `dist/` without
rewriting it. This keeps the module UI byte-identical to the app build.

## Single-source versioning

The repository-root `version.mk` is the single source of truth for the Canoe
version, module version code, and Web UI release pin. Do not copy version or
digest values into documentation: read `CANOE_VERSION`, `CANOE_VERSION_CODE`,
`CANOE_WEBUI_VERSION`, and `CANOE_WEBUI_SHA256` directly from `version.mk`.

Run `make bump VERSION=<release-version> VERSION_CODE=<release-version-code>`
to regenerate derived version files, then run `make version-check`. The gate
checks the generated host and module metadata, the pinned Web UI archive and
its URL, the BDS's `canoe-bds` and menu strings, the build stamp cache, and
the BDS bytes embedded in any existing package archives.

## Byte-identical boot artifacts

Every toolkit and module package carries the same bytes for:

- `BDS.efi`; and
- the standalone EFI tools `ArbTools.efi`, `BLTools.efi`, `RebootTools.efi`,
  `SurfaceTools.efi`, and `UsbTools.efi`.

The package recipes build each EDK2 artifact once per workspace and reuse it
instead of relinking once per package. This is checked because EDK2 relinking
can produce different bytes from the same sources; rebuilding separately would
make packages disagree about the boot menu or its standalone tools. Byte
identity makes the shipped boot behavior and the artifact provenance
unambiguous.

When UEFI sources change, force one clean BDS rebuild for the package command:

```bash
UEFI_REBUILD=1 make target_toolkit_linux
```

Use the matching package target when the final package is Windows, Android, or
the module. Do not force a separate rebuild for each package.

## Host command surface

The host toolkit is GUI-first. Its root contains the launcher and CLI; `bin/`
contains the desktop app and its sidecar:

```text
canoe-boot-manager.sh       # Linux GUI launcher
canoe-boot-manager.bat      # Windows GUI launcher
canoe                       # Linux CLI
canoe.exe                   # Windows CLI
bin/canoe-boot-manager      # Linux desktop binary
bin/canoe-boot-manager.exe  # Windows desktop binary
bin/canoe-bootmgr           # boot-root writer sidecar
```

The CLI remains useful for automation and does not require WebKitGTK or
WebView2:

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

Build the native host CLI with:

```bash
cargo build --locked --release --manifest-path tools/canoe/Cargo.toml
```

For its Windows GNU target, use the rustup target and MinGW linker:

```bash
CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER=x86_64-w64-mingw32-gcc \
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
`extractfv -o <workdir> -v <abl>`, and requires `<workdir>/LinuxLoader.efi`.
It then runs `patch_abl <workdir>/LinuxLoader.efi <staged>/boot.efi`, requiring
a non-empty output, followed by
`mode2_profile derive --vbmeta <vbmeta> --out <staged>/boot.efi.gm2p` and its
`validate` command. Finally it runs
`abl_tzmap derive <workdir>/LinuxLoader.efi -o <staged>/boot.efi.tzmap --allow-incomplete`,
then validates and verifies that sidecar against the extracted loader with
`--allow-zero-digest`. The profile must be exactly 120 bytes and the map
exactly 256 bytes. On success, the staging directory contains exactly
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
containing the running `canoe-bootmgr`, then `PATH`. A missing tool is an error
naming that tool. Any failure removes all three staged outputs and any
`--keep-unpatched` or `--patch-log` file created by that invocation.

`canoe build` is the host convenience surface for this same orchestrator; it
does not maintain a separate derivation implementation. By default it reads
`images/abl.img` and `images/vbmeta.img`; `--abl` and `--vbmeta` copy supplied
files into those canonical locations before deriving. The images must match
the firmware being booted.

`canoe install` validates and commits the staged boot root for the required
active slot. Without `--boot-root`, the host reaches the boot root through the
BDS `fastboot oem mass-storage:persist` export. A provided `--boot-root` points
to an already mounted `persist/efisp` directory. `--vendor-boot IMG` creates a
patched copy for the selected slot and reports the corresponding fastboot
flash; the source image is never modified. `--allow-new-signer` permits an
expected signer change when moving to or from a custom ROM.

## Host derivation tools

The Linux and Android packages contain `extractfv`, `patch_abl`, `mode2_profile`,
and `abl_tzmap`. The Windows package contains their `.exe` forms.
`mode2_profile` provides `derive` and `validate` for the 120-byte KeyMint
profile. `abl_tzmap` derives and validates the 256-byte `GTZM` map from the
unpatched ABL and accepts incomplete reverse-engineering evidence.

The `vbmetaport` utility remains available as the standalone recovery-vbmeta
graft tool referenced by the Mode 1 questionnaire. No boot-image binary is
bundled: the host `vendor_boot` feature is a fixed-offset, in-place cmdline
amendment.

## Build a matching pair

Place matching stock images at:

```text
images/abl.img
images/vbmeta.img
```

Then run:

```bash
./canoe build
```

The result is a patched `boot.efi`, its exact 120-byte `boot.efi.gm2p`, and a
256-byte `boot.efi.tzmap`. The map is derived from the unpatched ABL. The
installation transaction copies all required files together and rolls the tree
back if a commit fails.

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

## Windows package and ext4 helper

The Windows archive bundles the GUI launcher, `canoe-boot-manager.exe`, the
native `canoe.exe`, `fastboot.exe`, and `canoe-ext4.exe`. No Python installation
or bundled interpreter is needed. No drive letter, filesystem driver, or mount
is involved: `canoe.exe install --slot <A|B>` asks
`canoe-bootmgr source detect --json` for the exported source and runs the
boot-root transaction against the raw `\\.\PhysicalDrive<N>` source.

To build the Windows ext4 helper from source, provide e2fsprogs and zlib:

```bash
E2FSPROGS_SRC=/path/to/e2fsprogs ZLIB_PREFIX=/path/to/zlib \
  tools/canoe-ext4/build-windows.sh
```

Packaging fails if `canoe-ext4.exe` is absent; there is no placeholder or
silent fallback. To inspect a raw disk by hand:

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
