# Release Runbook

A Canoe release is produced by two repositories. The firmware repository assembles the
firmware, host, Android, and KernelSU packages; the separate
[`canoe-boot-manager`](https://github.com/1vivy/canoe-boot-manager) repository produces
the Svelte 5 + Vite `dist/` and the Tauri desktop applications. The desktop application
is a new program, not the retired egui `tools/canoe-gui`; the module WebUI is also the
same `dist/`, not the retired hand-written `targets/magisk_module/module/webroot`.
`canoe-bootmgr` remains the only writer and the application speaks its JSON wire
protocol.

Run all firmware commands from this worktree:

```sh
cd /home/vivy/Projects/efisp-projects/gbl_root_canoe/.work/gui-work
```

## 1. Version, imports, and release order

`version.mk` is the source of the Canoe release version. Do not edit generated
version files by hand. A release follows this order from the firmware worktree:

```sh
make bump VERSION=7.0.0-b5 VERSION_CODE=18
```

A direct `CANOE_VERSION=<other-version>` override is refused before any recipe
runs. A throwaway local build may opt in with `CANOE_VERSION_OVERRIDE=1`; its
effective BDS version receives a `-local` suffix and the release gate rejects
that artifact. Never use the opt-in for a release.

Next refresh every import that moved and pin it through the manifest interface:

```sh
make import-pin ID=<id>
```

Run that command once for each changed hashable import. For fetch rows, update
the URL and digest together in `imports.toml`; they have no local file for
`import-pin` to hash. For subtree, data, satellite, and external rows, follow
the per-import procedures below. Then run the gate:

```sh
make version-check
```

Only after the gate passes, build the affected packages. A release that changes
the boot chain should rebuild BDS once and then reuse it:

```sh
UEFI_REBUILD=1 make target_toolkit_linux target_toolkit_windows
make target_toolkit_android target_magisk_module
```

The manifest is the authority for import identity. Digests intentionally never
appear in this runbook: `imports.toml` records them and `make version-check`
proves them. A successful gate does not replace the package-specific tests in
Section 4.

### Per-import bump recipes

**`webui` artifact.** In the app repository, run its typecheck, tests, and
build, then create the deterministic archive. From the firmware worktree:

```sh
make -C targets/magisk_module webui-pin \
  CANOE_WEBUI_SRC=../../../canoe-boot-manager
make import-pin ID=webui
```

When the app version changes, pass the same new value as
`CANOE_WEBUI_VERSION=<webui-version>` to `webui-pin` and
`VERSION=<webui-version>` to `make import-pin`; this keeps the archive path,
URL, and manifest version aligned.

The deterministic-tarball procedure in Section 2 is authoritative for how the
archive is produced.

**`msd-variant` artifact.** In the `canoe-msd` checkout, rebuild the calibrated
blob with its normal `make patch BLOB=... OUT=... CALIBRATION=...` command.
Copy the resulting `canoe-usbmsd.efi` to
`submodules/uefi/blobs/canoe-usbmsd.efi`, then run:

```sh
make import-pin ID=msd-variant
```

After the release-order `make version-check` passes, rebuild the BDS and every
affected package.

**`platform-tools` fetch.** When the upstream archive changes, edit its `url`
and `sha256` together in `imports.toml`, then run `make version-check`. Do not
record a URL without its matching digest.

**`xz-utils` fetch.** This row currently has no recorded digest. Keep its URL
and `pinned = false` explicit, and run `make version-check`; do not present the
fetch as verified until an upstream digest is available and recorded.

**`edk2-vendor` subtree.** Re-squash the vendored EDK2 source from the upstream
repository, preserving the declared exclusions. Set `upstream.rev` to the
source revision used and `imported_at` to the new commit in this repository.
Keep local Canoe patches outside the imported files and set
`local_patches = true` when they are intentional; the gate reports that drift.

**`ablrepo` data.** Add or replace one product directory using the ingest recipe
in `ablrepo/README.md`: copy `abl.img`, generate `abl.sha256`, and write
`abl.meta` from measured identity data. Commit the entry in this repository,
then run `make version-check`; the gate checks every entry.

**`android-efi-tools` satellite.** Update `VERSION_STRING` independently in
each declared INF and update the matching `[import.versions]` entries in
`imports.toml`: `ArbTools.inf`, `BLTools.inf`, `CrashTools.inf`,
`LogTools.inf`, `MdTools.inf`, `RebootTools.inf`, `SurfaceTools.inf`,
`UsbTools.inf`, `Library/AndroidToolsUi/AndroidToolsUi.inf`, and
`Library/MdTableLib/MdTableLib.inf`. The current expected set is
`0.1` for every listed file except `SurfaceTools.inf`, which is `0.2`. Keep
the versions consistent with the satellite change, but do not change
`CANOE_VERSION`: this project is deliberately not versioned by Canoe. Run
`make version-check`.

**`desktop-app-linux` external input.** Supply the Linux binary through
`CANOE_APP_LINUX_BIN` from the operator's sibling app checkout. The row is
always reported as `EXTERNAL` and cannot be pinned; after the release-order
gate passes, rebuild the affected Linux package.

**`desktop-app-windows` external input.** Supply the Windows GNU binary through
`CANOE_APP_WINDOWS_BIN` from the operator's sibling app checkout. The row is
always reported as `EXTERNAL` and cannot be pinned; after the release-order
gate passes, rebuild the affected Windows package.

The two desktop binaries cannot be pinned today because
`canoe-boot-manager` has no remote and no tags today. The `xz-utils` row is
the other honest release gap: it is fetched without a digest. Neither gap may
be hidden by copying an unverified value into prose.

## 2. Build the app repository first

The app repository is:

```sh
cd /home/vivy/Projects/efisp-projects/canoe-boot-manager
```

Run its three gates in this order:

```sh
bun run typecheck
bun test
bun run build
```

`bun run typecheck` must report zero errors **and zero warnings**. `bun test`
must pass the complete protocol and UI suite. `bun run build` runs
`vite build` and then `bun run check-assets`; the asset guard rejects absolute
`/assets` URLs because the same output is loaded from a `file://` origin by the
KernelSU WebUI and by Tauri.

CI also asserts the protocol catalogue explicitly. The command is the
authority for the current verb and request/response fixture matrices:

```sh
bun run check-protocol-catalogue
```

The app repository itself publishes only the deterministic WebUI tarball. It does not
publish desktop binaries: a clean app tag has no target-compatible `canoe-bootmgr`
sidecar, and the sidecar must be built from the firmware repository's source.

Finally create the deterministic WebUI release asset:

```sh
bun run dist-tarball
```

This emits `canoe-boot-manager-<version>.tar.gz` from `dist/`, sorting members, zeroing
mtimes, pinning owner/group and numeric IDs, and pinning gzip output. Record the printed
SHA-256 and use that exact value for `CANOE_WEBUI_SHA256` in the firmware repository.
The tag workflow uses this exact invocation (with its temporary output path):

```sh
APP_VERSION="$(bun -e 'console.log((await Bun.file("package.json").json()).version)')"
ARCHIVE="$RUNNER_TEMP/canoe-boot-manager-${APP_VERSION}.tar.gz"
bun run dist-tarball -- "$ARCHIVE"
sha256sum "$ARCHIVE" | cut -d ' ' -f 1
```

A deterministic implementation must produce the same digest when run twice for the
same `dist/`. Prove that locally with a fixed output path:

```sh
APP_VERSION="$(bun -e 'console.log((await Bun.file("package.json").json()).version)')"
ARCHIVE="canoe-boot-manager-${APP_VERSION}.tar.gz"
bun run dist-tarball -- "$ARCHIVE"
sha256sum "$ARCHIVE"
bun run dist-tarball -- "$ARCHIVE"
sha256sum "$ARCHIVE"
```

A deterministic implementation produces the same digest both times. The digest
itself is deliberately not written here: it changes with every app build, so a copy
in prose is stale the moment the app is rebuilt, and a stale digest in a release
guide is worse than none. The authoritative value is `CANOE_WEBUI_SHA256` in
`version.mk`, and `make version-check` fails when the checked-in archive does not
hash to it. The app version comes from `package.json`; once published, the release
URL and digest must move together. The firmware repository consumes the tarball by
URL and SHA-256; it does not rebuild or reinterpret the WebUI.

## 3. Build the firmware packages

Return to the firmware worktree:

```sh
cd /home/vivy/Projects/efisp-projects/gbl_root_canoe/.work/gui-work
```

The firmware release builds the desktop binaries after it has checked out the app and
built every target-compatible Tauri input. Tauri hashes each input at build time, so
stage the complete set rather than only `canoe-bootmgr`.

Build the Linux inputs and stage the six required target-triple names:

The Linux stage input names are
`canoe-bootmgr-x86_64-unknown-linux-gnu`,
`canoe-ext4-x86_64-unknown-linux-gnu`,
`extractfv-x86_64-unknown-linux-gnu`,
`patch_abl-x86_64-unknown-linux-gnu`,
`mode2_profile-x86_64-unknown-linux-gnu`, and
`abl_tzmap-x86_64-unknown-linux-gnu`.

```sh
make -C targets/toolkit_linux \
  submodule_canoe_bootmgr submodule_canoe_ext4 submodule_ablfvextractor \
  submodule_patcher submodule_mode2_profile submodule_abl_tzmap
APP=/home/vivy/Projects/efisp-projects/canoe-boot-manager
for name in canoe-bootmgr canoe-ext4 extractfv patch_abl mode2_profile abl_tzmap; do
  cp "targets/toolkit_linux/build/toolkit/bin/$name" \
    "$APP/src-tauri/binaries/${name}-x86_64-unknown-linux-gnu"
done
(cd "$APP" && bunx tauri build --no-bundle --ci)
```

Do not stage Linux `fastboot` in the app inputs. At runtime Linux selects an
executable `fastboot` (or `fastboot.exe`) from the inherited `PATH`, then opens
and seals that external executable for the session.

Build the Windows GNU inputs, including the adjacent Platform-Tools files, and
stage the nine required names:

The Windows stage input names are
`canoe-bootmgr-x86_64-pc-windows-gnu.exe`,
`canoe-ext4-x86_64-pc-windows-gnu.exe`,
`extractfv-x86_64-pc-windows-gnu.exe`,
`patch_abl-x86_64-pc-windows-gnu.exe`,
`mode2_profile-x86_64-pc-windows-gnu.exe`,
`abl_tzmap-x86_64-pc-windows-gnu.exe`,
`fastboot-x86_64-pc-windows-gnu.exe`,
`AdbWinApi-x86_64-pc-windows-gnu.dll`, and
`AdbWinUsbApi-x86_64-pc-windows-gnu.dll`.

```sh
make -C targets/toolkit_windows \
  submodule_canoe_bootmgr submodule_canoe_ext4 submodule_ablfvextractor \
  submodule_patcher submodule_mode2_profile submodule_abl_tzmap \
  platform_tools ext4_tools
for name in canoe-bootmgr canoe-ext4 extractfv patch_abl mode2_profile abl_tzmap; do
  cp "targets/toolkit_windows/build/toolkit/bin/$name.exe" \
    "$APP/src-tauri/binaries/${name}-x86_64-pc-windows-gnu.exe"
done
cp targets/toolkit_windows/build/toolkit/Platform-Tools/fastboot.exe \
  "$APP/src-tauri/binaries/fastboot-x86_64-pc-windows-gnu.exe"
cp targets/toolkit_windows/build/toolkit/Platform-Tools/AdbWinApi.dll \
  "$APP/src-tauri/binaries/AdbWinApi-x86_64-pc-windows-gnu.dll"
cp targets/toolkit_windows/build/toolkit/Platform-Tools/AdbWinUsbApi.dll \
  "$APP/src-tauri/binaries/AdbWinUsbApi-x86_64-pc-windows-gnu.dll"
(cd "$APP" && \
  bunx tauri build --target x86_64-pc-windows-gnu --no-bundle --ci)
```

The Windows runtime privately stages `bin/*.exe` and the three adjacent
`Platform-Tools` files, verifies each against its build-time digest, and denies
replacement while the sidecar can spawn. It does not resolve these inputs from
`PATH`. The MSVC app CI compile fixture uses the same helper stems with
`x86_64-pc-windows-msvc` names (and `.exe`/`.dll` extensions); those fixture
inputs are not release artifacts.

After these Tauri builds, the package recipes copy each app binary beside
`bin/canoe-bootmgr` (or `.exe`) and retain the helper adjacency in the toolkit.

This is a git worktree. Its normal sibling default for the app points relative to the
worktree, not to `/home/vivy/Projects/efisp-projects/canoe-boot-manager`; therefore
pass both absolute app-binary overrides explicitly. Build Linux and Windows together,
forcing one clean BDS rebuild for the invocation:

```sh
PATH="$HOME/.cargo/bin:$PATH" UEFI_REBUILD=1 make \
  CANOE_APP_LINUX_BIN=/home/vivy/Projects/efisp-projects/canoe-boot-manager/src-tauri/target/release/canoe-boot-manager \
  CANOE_APP_WINDOWS_BIN=/home/vivy/Projects/efisp-projects/canoe-boot-manager/src-tauri/target/x86_64-pc-windows-gnu/release/canoe-boot-manager.exe \
  target_toolkit_linux target_toolkit_windows
```

Build Android and the module against the rustup toolchain and the Android NDK:

```sh
PATH="$HOME/.cargo/bin:$PATH" make \
  NDK_PATH=/opt/android-ndk \
  target_toolkit_android target_magisk_module
```

`UEFI_REBUILD=1` forces a from-scratch BDS exactly once for the whole `make`
invocation. In a release workspace the BDS is built at most once and then reused by
the remaining package targets; do not pass `UEFI_REBUILD=1` again for another package.
Without it, an existing `submodules/uefi/build/BDS.efi` is deliberately reused. The
EDK II link is not reproducible byte-for-byte when repeated, so each package target
copies the already-built BDS instead of relinking it. This is why every package must
carry byte-identical `BDS.efi`; do not run a separate clean UEFI build for each package.

The resulting archives are written below the package build directories. Inspect their
members before publishing; archive creation alone does not prove the package contract.

## 4. Gates before creating a tag

Run every gate below from the firmware worktree.

### Firmware and Rust tests

```sh
make test
make -C submodules/uefi test
make -C tools/canoe-ext4 test
cargo test --locked --manifest-path tools/canoe-bootmgr/Cargo.toml
cargo test --locked --manifest-path tools/mode2-profile/Cargo.toml
cargo test --locked --manifest-path tools/abl-tzmap/Cargo.toml
```

`make test` includes the repository's aggregate shell, Rust, patcher, UEFI, and ext4
checks. A known host-only race can occasionally report `ExecutableFileBusy` while the
`canoe` test binary is being replaced; rerun `make test` in that case and investigate
any failure that persists. The retired `tools/canoe-gui` crate is not a release gate.

### Version and shared BDS bytes

```sh
make version-check
```

Prove that the linked BDS and the BDS extracted from every shipped package archive
have one SHA-256. The archive names below are the four package outputs currently
assembled by this repository; if a release profile omits a package, remove only that
profile's archive from the list, never the comparison itself:

```sh
set -eu
linked='submodules/uefi/build/BDS.efi'
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
for package in toolkit_linux toolkit_windows toolkit_android magisk_module; do
  mkdir -p "$tmp/$package"
done
unzip -q targets/toolkit_linux/build/toolkit_linux.zip -d "$tmp/toolkit_linux"
unzip -q targets/toolkit_windows/build/toolkit_windows.zip -d "$tmp/toolkit_windows"
unzip -q targets/toolkit_android/build/toolkit_android.zip -d "$tmp/toolkit_android"
unzip -q targets/magisk_module/build/module_android.zip -d "$tmp/magisk_module"
for file in BDS.efi efisp/tools/RebootTools.efi efisp/tools/BLTools.efi \
            efisp/tools/ArbTools.efi efisp/tools/SurfaceTools.efi; do
  sha256sum "$linked" "$tmp"/toolkit_linux/"$file" \
    "$tmp"/toolkit_windows/"$file" "$tmp"/toolkit_android/"$file" \
    "$tmp"/magisk_module/"$file"
  cmp "$linked" "$tmp/toolkit_linux/$file"
  cmp "$tmp/toolkit_linux/$file" "$tmp/toolkit_windows/$file"
  cmp "$tmp/toolkit_linux/$file" "$tmp/toolkit_android/$file"
  cmp "$tmp/toolkit_linux/$file" "$tmp/magisk_module/$file"
done
```

The command compares the linked BDS and every archive copy, plus the four shared
standalone EFI tools. Each `sha256sum` group must contain one digest repeated five
times, and every `cmp` must succeed. The digest is intentionally not pinned in this
document: it changes whenever BDS sources change.

### Windows executable imports

For every shipped Windows `.exe` (including the desktop app and the Rust helpers),
run:

```sh
x86_64-w64-mingw32-objdump -p <exe> | grep 'DLL Name'
```

The output may list Windows system/UCRT DLLs only. Any `libwinpthread-1.dll`,
`libgcc_s_seh-1.dll`, or `libstdc++-6.dll` import is a shipping failure: it would
require a MinGW runtime that is not bundled as part of the supported package contract.

### WebUI identity

The module's shipped WebUI must be byte-identical to the app's `dist/`, not merely
similar or present. Compare the trees after extracting/copying the pinned archive:

```sh
diff -r targets/magisk_module/build/module/webroot \
  /home/vivy/Projects/efisp-projects/canoe-boot-manager/dist
```

`diff -r` must produce no output and exit successfully. This gate covers the complete
asset tree (the current verification traverses 81 archive members), including
`index.html` and hashed files under `assets/`.

Only after all gates pass should the app repository tag publish its one deterministic
tarball asset and the firmware repository tag publish the package archives.

## 5. Deliberately not proven here

The Windows desktop application builds through both the GNU and MSVC routes, but its
runtime is **unproven from this host**: there is no real Windows machine available and
there is no WebView2 runtime under Wine. “It builds” is the only claim this runbook
makes for that surface. No NSIS or MSI installer is produced here.
