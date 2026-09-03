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

## 1. Set and check the version

`version.mk` is the single version source. Do not edit generated version files by
hand. To change a release, pass the new values to `make bump`; it regenerates the
derived Rust version and module metadata. Then prove that nothing drifted:

```sh
make bump VERSION=7.0.0-b2 VERSION_CODE=15
make version-check
```

For a release that keeps the values already in `version.mk`, the equivalent command is
simply:

```sh
make bump
make version-check
```
A direct `CANOE_VERSION=<other-version>` override is refused before any
recipe runs. A throwaway local build may opt in with
`CANOE_VERSION_OVERRIDE=1` on the make command line; its effective BDS version
receives a `-local` suffix and `make version-check` rejects the non-release
artifact. Never use that opt-in for a release.


The WebUI pin is the `CANOE_WEBUI_VERSION`, `CANOE_WEBUI_SHA256`, and
`CANOE_WEBUI_URL` trio in `version.mk`. For a tagged release, the URL and digest point
at the app repository's published `canoe-boot-manager-<version>.tar.gz`, which contains
the app's deterministic `dist/`. Before that asset is published, the checked-in
`targets/magisk_module/webui-cache/` archive is the last-known-good fallback and the
URL may be its `file://` path. `make version-check` verifies that this fallback archive
exists and has the pinned bytes; update the release URL and pin only after obtaining the
digest printed by the app release.

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

`bun run typecheck` must report zero errors **and zero warnings**. `bun test` replays the
protocol and UI suite (the current baseline is 216 passing tests and 924 expectations
in 24 files). `bun run build` runs `vite build` and then `bun run check-assets`; the
asset guard rejects absolute `/assets` URLs because the same output is loaded from a
`file://` origin by the KernelSU WebUI and by Tauri.

CI also asserts the protocol catalogue explicitly; it must remain 34 verbs with 17
request/response fixture pairs:

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
built the target-compatible `canoe-bootmgr` sidecars from this repository. Stage each
sidecar beside the app's Tauri inputs, then build the desktop binary:

```sh
make -C targets/toolkit_linux submodule_canoe_bootmgr
cp targets/toolkit_linux/build/toolkit/bin/canoe-bootmgr \
  /home/vivy/Projects/efisp-projects/canoe-boot-manager/src-tauri/binaries/canoe-bootmgr-x86_64-unknown-linux-gnu
(cd /home/vivy/Projects/efisp-projects/canoe-boot-manager && \
  bunx tauri build --no-bundle --ci)
make -C targets/toolkit_windows submodule_canoe_bootmgr
cp targets/toolkit_windows/build/toolkit/bin/canoe-bootmgr.exe \
  /home/vivy/Projects/efisp-projects/canoe-boot-manager/src-tauri/binaries/canoe-bootmgr-x86_64-pc-windows-gnu.exe
(cd /home/vivy/Projects/efisp-projects/canoe-boot-manager && \
  bunx tauri build --target x86_64-pc-windows-gnu --no-bundle --ci)
```

The optional MSVC build uses the Tauri target below when `cargo-xwin` is installed:

```sh
(cd /home/vivy/Projects/efisp-projects/canoe-boot-manager && \
  bunx tauri build --target x86_64-pc-windows-msvc --no-bundle --ci)
```

The published Windows desktop asset is the GNU target; the MSVC command is a
build-path check, not an installer-production step. Tauri places each sidecar beside
its application executable in the shipped package, so each final toolkit must contain
both `bin/canoe-boot-manager` (or its `.exe` form) and `bin/canoe-bootmgr`.

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
