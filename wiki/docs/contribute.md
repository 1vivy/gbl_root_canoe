# Contribution Guide

## Repository split

Canoe has two repositories and a deliberate ownership boundary:

- The firmware repository owns boot and menu behavior in
  `QcomModulePkg/Application/LinuxLoader`.
- `canoe-bootmgr` owns boot-root writes and the `canoe.cfg` grammar. It is the
  only writer; callers must use its JSON wire protocol rather than implementing
  another transaction path.
- The separate `canoe-boot-manager` repository owns presentation: the Svelte 5
  + Vite application used by the Tauri desktop shell and by the KernelSU
  Android WebUI. It does not own boot-root mutation.
- `targets/` owns packaging, launcher files, artifact pins, and placement of
  the desktop binary beside its `canoe-bootmgr` sidecar.

Keep changes in the repository that owns the behavior. A UI change belongs in
`/home/vivy/Projects/efisp-projects/canoe-boot-manager`; a BDS or boot-menu
change belongs in this firmware repository. Do not add a second presentation
or writer implementation here.

## Verification by layer

Run the smallest relevant check while developing, then run the full regression
before submitting.

### BDS and boot/menu behavior

For changes under `QcomModulePkg/Application/LinuxLoader`, run the root UEFI
regression and, when a package artifact is needed, force one clean BDS rebuild:

```bash
make test
UEFI_REBUILD=1 CANOE_APP_LINUX_BIN=/absolute/path/to/canoe-boot-manager/src-tauri/target/release/canoe-boot-manager \
  make target_toolkit_linux
```

Use the corresponding root `target_toolkit_windows`,
`target_toolkit_android`, or `target_magisk_module` target for the package you
are checking. The package target must receive the absolute
`CANOE_APP_LINUX_BIN` or `CANOE_APP_WINDOWS_BIN` path when the app checkout is
not the default sibling checkout.

### `canoe-bootmgr` and config grammar

For changes to the boot-root transaction, JSON protocol, or `canoe.cfg` parser,
run its locked Rust tests:

```bash
cargo test --locked --manifest-path tools/canoe-bootmgr/Cargo.toml
```

The boot manager remains the only writer. Changes to its public protocol must
migrate every caller, including the app repository, rather than adding a second
or compatibility writer.

### Desktop and Android presentation

In `/home/vivy/Projects/efisp-projects/canoe-boot-manager`, install the pinned
Bun dependencies and run the app's checked-in gates:

```bash
bun install --frozen-lockfile
bun run typecheck
bun run build
bun test
```

`bun run build` verifies the static asset paths needed by both Tauri and
KernelSU. `bun test` runs the app tests and the same asset guard. Build a real
Tauri binary only after the target-compatible `canoe-bootmgr` sidecar is staged
in `src-tauri/binaries/`.

### Packaging

From this firmware repository, verify version pins and then the package target
that changed:

```bash
make version-check
make target_toolkit_linux
make target_toolkit_windows
make target_toolkit_android
make target_magisk_module
```

The package recipes consume the pinned app `dist/` archive for the Android
module through `fetch-verified`; they must not regenerate a divergent UI. They
also assert that the desktop app and its `canoe-bootmgr` sidecar remain
adjacent, and that BDS plus the standalone EFI tools retain byte identity across
packages.

## Contribution workflow

1. Fork the repository that owns the change.
2. Make the change in that repository and update every affected caller or
   package input.
3. Run the layer-specific commands above, followed by `make test`,
   `make version-check`, and the affected package target.
4. Inspect the generated archive and the changed files; do not commit generated
   or unrelated files.
5. Submit a pull request with the checks you ran and any platform limitation
   that remains. Never claim that a Linux Windows cross-build proves WebView2
   behavior on a real Windows installation.

## Copyright and license

- You retain the copyright to code you create.
- You agree to license and release your contribution under the **GPL license**.
- No Contributor License Agreement (CLA) is required.

## Attribution

If you provide patch ideas, reverse-engineering analysis, or related
contributions in issues, your name may be co-attributed in the corresponding
commit to acknowledge your contribution.
