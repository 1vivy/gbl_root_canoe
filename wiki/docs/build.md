# Build Guide

## Two repositories, one release

A Canoe release is built from two repositories. This firmware repository
contains BDS, the standalone EFI tools, the mounted-root commands, and the
package recipes. The separate `canoe-boot-manager` repository contains the
Svelte 5 + Vite application and its native Android worker. Build the app
checkout first; it emits two independent bundles:

- `dist/hosted`, served over HTTPS as the hosted Canoe Boot Manager; and
- `dist/ksu`, a self-contained bundle packaged into the KernelSU module.

There is no Tauri shell and no desktop executable sidecar. `7.0.0-b4-final`
preserves that stack. Mounted-root commands and image producers remain shared
libraries outside the application workflows.

### Build the app repository

From `/path/to/canoe-boot-manager`:

```bash
bun install --frozen-lockfile
bun run build
cargo build --locked --manifest-path native/canoe-manager/Cargo.toml
bun run check
bun test
```

These are the app repository's actual package scripts; there is no `typecheck`
script. `check` runs `svelte-check --fail-on-warnings`, so a warning fails it as
well as an error. Building needs Rust with the `wasm32-unknown-unknown` target
and `wasm-bindgen-cli` 0.2.128; set `WASM_BINDGEN` when that executable is
outside `PATH`. A successful build produces `dist/hosted` and `dist/ksu`. The
hosted output must keep the COOP/COEP headers in `dist/hosted/_headers`, which
not every server applies automatically. The KSU bundle uses local CSS and its
native worker, without hosted scripts, dynamic modules or SharedArrayBuffer.

### Stage the KSU bundle for the module

The module package consumes `dist/ksu` directly through `CANOE_KSU_DIST`, which
defaults to a sibling `canoe-boot-manager` checkout:

```bash
CANOE_KSU_DIST=/absolute/path/to/canoe-boot-manager/dist/ksu make target_magisk_module
```

`scripts/stage_ksu.py` refuses anything that is not a version-matched KSU build.
The directory needs `index.html`; its `manifest.json` must declare
`product=canoe-boot-manager`, the current `CANOE_VERSION`, and `runtime=ksu`;
and the tree must contain no symlinks. A hosted bundle, or a WebUI archive from
an earlier version, is rejected rather than packaged.

`imports.toml` records this input as the `external` row `ksu-app`. `make
version-check` reports it as `EXTERNAL` and neither hashes nor fetches it, so
staging the correct bundle is the operator's responsibility.

## Build prerequisites

A working release environment has all of the following:

- Docker, for the canonical EDK2/BDS build;
- the Android NDK, with `NDK_PATH` set or discovered from
  `ANDROID_NDK_LATEST_HOME`, `ANDROID_NDK_HOME`, then `ANDROID_NDK_ROOT`, for
  the rooted-Android one-shot installer and KernelSU module;
- a rustup-managed Rust toolchain, including the
  `aarch64-linux-android` standard library. A distro `cargo` shim without that
  rustup target fails with `can't find crate for std`;
- `mingw-w64`, including `x86_64-w64-mingw32-gcc`, only for the optional
  `tools_vbmetafixer_windows` target; and
- Bun, plus Rust's `wasm32-unknown-unknown` target, for the app repository.

Check the Rust targets with the rustup toolchain's `rustup target list --installed`.
The Rust crates currently require Rust 1.85 or newer. The NDK builds
specifically use the NDK's `aarch64-linux-android31-clang` linker.

## Build the release packages

From the firmware repository root, build the rooted-Android one-shot archive
with the current firmware output. Build the KernelSU module after the app's
`dist/ksu` has been produced:

```bash
make target_one_shot_android
make target_magisk_module
```

Both require an Android NDK. Set `NDK_PATH` explicitly, or let the build use the
first non-empty value from `ANDROID_NDK_LATEST_HOME`, `ANDROID_NDK_HOME`, and
`ANDROID_NDK_ROOT`. The resulting archives are
`targets/one_shot_android/build/canoe-one-shot-<CANOE_VERSION>-android-arm64.zip`
and `targets/magisk_module/build/module_android.zip`. The one-shot archive is
not a general toolkit: it contains `install-canoe.sh`, the four ARM64 Android
commands that script invokes, BDS, and the reviewed EFI tools.

Desktop toolkit packages are retired and have no package targets in this
repository. Use the hosted CANOE BOOT MANAGER for the desktop workflow.

The module recipe stages `CANOE_KSU_DIST` into the module webroot through
`scripts/stage_ksu.py`, which keeps the packaged UI byte-identical to the app's
KSU build and rejects a product, version or runtime mismatch.

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

The Android one-shot and module packages carry the same bytes for:

- `BDS.efi`; and
- the standalone EFI tools `ArbTools.efi`, `BLTools.efi`, `RebootTools.efi`,
  `SurfaceTools.efi`, and `UsbTools.efi`.

`make -C submodules/uefi tools` builds three more that neither package carries:
`LogTools.efi`, `MdTools.efi`, and `CrashTools.efi`. They are launched from a
host with `fastboot boot <tool>.efi` against a device already running a
vulnerable ABL, and they exist to investigate the firmware rather than to
operate it - `MdTools` scans and edits the Qualcomm minidump region table in
RAM, and `CrashTools` triggers deliberate faults to reach 900e memory-debug
mode. The packaged list above is therefore narrower than the build output on
purpose; do not "fix" it by adding them to a target.

The one-shot recipe requires existing canonical EDK2 output; the module recipe
builds missing EFI artifacts once per workspace. Reuse one firmware build when
assembling both packages. EDK2 relinking can produce different bytes from the
same sources; rebuilding separately would make packages disagree about the boot
menu or its standalone tools. Byte identity makes the shipped behavior and
artifact provenance unambiguous.

When UEFI sources change, force one clean BDS rebuild for the package command:

```bash
UEFI_REBUILD=1 make target_one_shot_android
```

Use `target_magisk_module` instead when the final package is the module. Do not
force a separate rebuild for each package.

## Command components and packaging

The [command guide](./commands.md) describes the current CLI. The interactive
`canoe` program and Android build.sh wrapper are retired. Build the command
crates under `tools/canoe-bootmgr`, `tools/canoe-image`, `tools/canoe-provision`,
and the sibling application's `native/canoe-manager` worker with locked Cargo
manifests. `tools/canoe-fs` shares confined file I/O without application policy.

`canoe-manager` is the Android native root worker, not the mounted-root CLI;
package its native dependencies beside it. Browser ext4 and FAT access belong to
the hosted app's Rust drivers over managed USB, so no libext2fs helper,
Ext4Windows/WinFsp driver or e2fsprogs fork is built or shipped.
`tools/canoe-ext4` is retained for reference only and is absent from release and
default test paths. Android harness builds use x86_64; the shipped module uses
ARM64. Both must contain current native tools and the same KSU bundle.

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

The device-series Linux artifacts are maintained outside this repository, in
[FantomTchi7/kaanapali-mainline-linux](https://github.com/FantomTchi7/kaanapali-mainline-linux),
branch `OnePlus-15-WIP`, commit `2d1ab8738563b8771e18b5939f00bb3361dd873a2`
(2026-04-22). The board DTS is
`arch/arm64/boot/dts/qcom/kaanapali-oneplus-infiniti.dts`; build its DTB with
`make ARCH=arm64 ... arch/arm64/boot/dts/qcom/kaanapali-oneplus-infiniti.dtb`.
It declares `compatible = "oneplus,infiniti"` and `dr_mode = "peripheral"`;
there is no `stdout-path`, and `uart7`/`uart18` are disabled. The arm64
defconfig materializes `EFI=y` and `EFI_STUB=y`; use an uncompressed `Image`.
H3 BLS paths under `persist` are `\\vmlinuz-canoe`,
`\\initramfs-canoe`, and
`\\dtbs\\kaanapali-oneplus-infiniti.dtb`.
