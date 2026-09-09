# Build Guide

## Two repositories, one release

A Canoe release is built from two repositories. The firmware repository contains
BDS, the boot manager, workers, host CLI, and package recipes. The separate
`canoe-boot-manager` repository contains the Svelte 5 + Vite application. Build
the app checkout first; its one static `dist/` is consumed twice:

- the Tauri desktop shell packages the same app for Linux or Windows; and
- the KernelSU module serves the same files as its Android WebUI.

The app speaks the JSON wire protocol to its `canoe-manager` sidecar. Mounted-root
commands and image producers remain shared libraries outside the application workflows.

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

The desktop binary is built separately with Tauri. Before that compile, stage
every target-compatible sidecar and helper in the app checkout's
`src-tauri/binaries/` directory. `src-tauri/build.rs` hashes each staged file
and embeds its SHA-256; a missing, non-regular, non-executable, or mismatched
target input fails the Tauri build. The Linux names are:

```text
canoe-manager-x86_64-unknown-linux-gnu
canoe-ext4-x86_64-unknown-linux-gnu
extractfv-x86_64-unknown-linux-gnu
patch_abl-x86_64-unknown-linux-gnu
mode2_profile-x86_64-unknown-linux-gnu
abl_tzmap-x86_64-unknown-linux-gnu
```

After building the Linux toolkit helpers, stage them with their target-triple
names:

```bash
APP=/absolute/path/to/canoe-boot-manager
for name in canoe-manager canoe-bootmgr canoe-image canoe-provision canoe-ext4 extractfv patch_abl mode2_profile abl_tzmap; do
  cp "targets/toolkit_linux/build/toolkit/bin/$name" \
    "$APP/src-tauri/binaries/${name}-x86_64-unknown-linux-gnu"
done
```

Linux `fastboot` is deliberately not staged in `src-tauri/binaries`: runtime
selects an executable `fastboot` (or `fastboot.exe`) from the inherited `PATH`,
then opens and seals that selected external input for the session.

The Windows GNU names are:

```text
canoe-manager-x86_64-pc-windows-gnu.exe
canoe-ext4-x86_64-pc-windows-gnu.exe
extractfv-x86_64-pc-windows-gnu.exe
patch_abl-x86_64-pc-windows-gnu.exe
mode2_profile-x86_64-pc-windows-gnu.exe
abl_tzmap-x86_64-pc-windows-gnu.exe
fastboot-x86_64-pc-windows-gnu.exe
AdbWinApi-x86_64-pc-windows-gnu.dll
AdbWinUsbApi-x86_64-pc-windows-gnu.dll
```

Stage the Windows GNU inputs from the toolkit's private runtime adjacency:

```bash
APP=/absolute/path/to/canoe-boot-manager
for name in canoe-manager canoe-bootmgr canoe-image canoe-provision canoe-ext4 extractfv patch_abl mode2_profile abl_tzmap; do
  cp "targets/toolkit_windows/build/toolkit/bin/$name.exe" \
    "$APP/src-tauri/binaries/${name}-x86_64-pc-windows-gnu.exe"
done
cp targets/toolkit_windows/build/toolkit/Platform-Tools/fastboot.exe \
  "$APP/src-tauri/binaries/fastboot-x86_64-pc-windows-gnu.exe"
cp targets/toolkit_windows/build/toolkit/Platform-Tools/AdbWinApi.dll \
  "$APP/src-tauri/binaries/AdbWinApi-x86_64-pc-windows-gnu.dll"
cp targets/toolkit_windows/build/toolkit/Platform-Tools/AdbWinUsbApi.dll \
  "$APP/src-tauri/binaries/AdbWinUsbApi-x86_64-pc-windows-gnu.dll"
```

The Windows MSVC app CI compile fixture uses the same nine stems with
`x86_64-pc-windows-msvc` in place of `x86_64-pc-windows-gnu` (and keeps
`.exe`/`.dll` extensions). Those fixture files are compile-only inputs, not
release artifacts. Do not use a placeholder for a release sidecar: Tauri
validates every required input before compiling.

For reference, the nine MSVC fixture names are:

```text
canoe-manager-x86_64-pc-windows-msvc.exe
canoe-ext4-x86_64-pc-windows-msvc.exe
extractfv-x86_64-pc-windows-msvc.exe
patch_abl-x86_64-pc-windows-msvc.exe
mode2_profile-x86_64-pc-windows-msvc.exe
abl_tzmap-x86_64-pc-windows-msvc.exe
fastboot-x86_64-pc-windows-msvc.exe
AdbWinApi-x86_64-pc-windows-msvc.dll
AdbWinUsbApi-x86_64-pc-windows-msvc.dll
```

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
`bin/canoe-manager`. The launchers in the toolkit root resolve that adjacency:
`canoe-boot-manager.sh` on Linux and `canoe-boot-manager.bat` on Windows.

The GNU-target Tauri build also emits `WebView2Loader.dll` beside the Windows
application. The Windows package requires and copies that loader into `bin/`.
When `CANOE_APP_WINDOWS_BIN` is overridden, the loader is taken from the same
directory unless `CANOE_WEBVIEW2_LOADER_WINDOWS` is set explicitly.

The standalone WebUI archive is pinned independently of the desktop binary.
The module's package recipe invokes the root `fetch-verified` target, verifies
its SHA-256 before and after fetching, and extracts the app's `dist/` without
rewriting it. This keeps the module UI byte-identical to the app build.

## Imports

This tree has two development patterns. The core project is edited, tested, and
released in the normal way; its release identity is `CANOE_VERSION` and
`CANOE_VERSION_CODE` in `version.mk`. Everything else arrives as an import:
either a squashed source copy, a compiled output produced by a sibling
repository, or foreign data. The root `imports.toml` manifest declares each
import once. `make version-check` checks that manifest, and
`make import-pin ID=<id>` is the one re-pinning entry point for imports that
can be hashed.

The manifest has one row per input. Its `kind` says what the check can prove:

- **`artifact`** is a compiled file carried by this repository. The gate hashes
  the declared `path` and compares both its digest and byte count with the
  manifest, reporting expected and actual values on a mismatch. This proves
  the checked-in file has not changed since it was pinned; it does not prove
  how the producer built it, that the producer source is present, or that the
  file is safe.
- **`fetch`** is an artifact downloaded at build time into a gitignored cache
  and never committed. The gate checks that the URL, digest (when pinned), and
  generated make variables are complete and agree; it does not hash a local
  file. A row with `pinned = false` is an unclosed supply-chain gap, not a
  verified input.
- **`subtree`** is a squashed source import. The gate compares the named path
  with the recorded `imported_at` commit, after removing declared exclusions.
  This proves only that the vendored copy has (or has not) changed locally
  since that import commit. It says nothing about upstream, the quality of the
  squash, or whether the local copy is current. Drift fails when
  `local_patches = false`; with `local_patches = true`, it is reported without
  failing.
- **`data`** is foreign data whose entries carry their own integrity records.
  The gate hashes every entry image and compares it with the entry digest file
  and the `sha256=` and `bytes=` values in its metadata. This proves internal
  consistency, not that the data came from the claimed device or that it will
  boot there.
- **`satellite`** is a project in this tree deliberately outside
  `CANOE_VERSION`. The gate checks each declared file's `version_key` against
  the expected per-file version. It does not prove that the satellite files
  are mutually compatible or that their source is unchanged.
- **`external`** is an operator-supplied input from a sibling checkout. The
  gate reports it as `EXTERNAL` and never fails or hashes it. This records the
  dependency and its producer command, but proves neither presence nor
  provenance of the supplied binary.

The manifest and generated `imports.mk` are the authority for import identity.
Do not copy digest values into build documentation; use the manifest and
`make version-check` instead. A successful gate is evidence only for the
specific checks above, not a substitute for building or testing the affected
package.

## Single-source versioning

The repository-root `version.mk` is the single source of truth for the Canoe
release version and module version code. Import identity is declared separately
in `imports.toml` and emitted for make consumers in the generated
`imports.mk`; neither file should be edited indirectly through documentation.
Do not copy version or digest values into documentation.

Run `make bump VERSION=<release-version> VERSION_CODE=<release-version-code>`
to regenerate derived version files. Refresh changed imports with
`make import-pin ID=<id>` where applicable, then run `make version-check`.
The gate checks generated metadata, every declared import, the BDS's
`canoe-bds` and menu strings, the build stamp cache, and the BDS bytes embedded
in any existing package archives.

## Byte-identical boot artifacts

Every toolkit and module package carries the same bytes for:

- `BDS.efi`; and
- the standalone EFI tools `ArbTools.efi`, `BLTools.efi`, `RebootTools.efi`,
  `SurfaceTools.efi`, and `UsbTools.efi`.

`make -C submodules/uefi tools` builds three more that no package carries:
`LogTools.efi`, `MdTools.efi`, and `CrashTools.efi`. They are launched from a
host with `fastboot boot <tool>.efi` against a device already running a
vulnerable ABL, and they exist to investigate the firmware rather than to
operate it - `MdTools` scans and edits the Qualcomm minidump region table in
RAM, and `CrashTools` triggers deliberate faults to reach 900e memory-debug
mode. The packaged list above is therefore narrower than the build output on
purpose; do not "fix" it by adding them to a target.

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

## Command components and packaging

The [command guide](./commands.md) describes the current CLI. The interactive
`canoe` program and Android build.sh wrapper are retired. Build the command
crates under `tools/canoe-bootmgr`, `tools/canoe-image`, `tools/canoe-provision`,
and the sibling application's `native/canoe-manager` worker with locked Cargo
manifests. `tools/canoe-fs` shares confined file I/O without application policy.

The application sidecar is `canoe-manager`, not the mounted-root CLI. Package
its native dependencies beside it. Windows uses native FAT access and the
unchanged libext2fs helper only for offline ext4 provisioning/removal:

```sh
E2FSPROGS_SRC=/path/to/e2fsprogs ZLIB_PREFIX=/path/to/zlib \
  tools/canoe-ext4/build-windows.sh
```

No Ext4Windows/WinFsp driver or e2fsprogs fork is required. Validate Windows
imports, native UAC/USB behavior and detached readback; compilation alone does
not establish those behaviors. Android harness builds use x86_64; the shipped
module uses ARM64. Both must contain current native tools and the same WebUI.

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
H3 BLS paths under `persist` are `\\vmlinuz-canoe`,
`\\initramfs-canoe`, and
`\\dtbs\\kaanapali-oneplus-infiniti.dtb`. The marker endpoint is
`telnet 192.168.42.1:2323`. Full provenance and the preparation script remain
under `.work/device-series`; they are not repository source files.
