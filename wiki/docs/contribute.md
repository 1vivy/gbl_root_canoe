# Contribution Guide

## Repository split

Canoe has two repositories and a deliberate ownership boundary:

- The firmware repository owns boot and menu behavior in
  `QcomModulePkg/Application/LinuxLoader`.
- `canoe-bootmgr` owns boot-root writes and the `canoe.cfg` grammar. It is the
  only writer; callers must use its JSON wire protocol rather than implementing
  another transaction path.
- The separate `canoe-boot-manager` repository owns presentation: the Svelte 5
  + Vite application served both as the hosted browser app and as the KernelSU
  Android WebUI. It does not own boot-root mutation.
- `targets/` owns packaging, artifact pins, and staging the version-matched
  `dist/ksu` bundle into the KernelSU module.

Keep changes in the repository that owns the behavior. A UI change belongs in
the `canoe-boot-manager` checkout; a BDS or boot-menu change belongs in this
firmware repository. Do not add a second presentation or writer implementation
here.

## Verification by layer

Run the smallest relevant check while developing, then run the full regression
before submitting.

### BDS and boot/menu behavior

For changes under `QcomModulePkg/Application/LinuxLoader`, run the root UEFI
regression and, when a package artifact is needed, force one clean BDS rebuild:

```bash
make test
UEFI_REBUILD=1 make target_magisk_module
```

Use `target_toolkit_android` instead for the temporary-root package. The module
target must receive an absolute `CANOE_KSU_DIST` path when the app checkout is
not the default sibling checkout. The retired `target_toolkit_linux` and
`target_toolkit_windows` targets refuse to run.

### `canoe-bootmgr` and config grammar

For changes to the boot-root transaction, JSON protocol, or `canoe.cfg` parser,
run its locked Rust tests:

```bash
cargo test --locked --manifest-path tools/canoe-bootmgr/Cargo.toml
```

The boot manager remains the only writer. Changes to its public protocol must
migrate every caller, including the app repository, rather than adding a second
or compatibility writer.

### Hosted and Android presentation

In the `canoe-boot-manager` checkout, install the pinned Bun dependencies and
run the app's checked-in gates:

```bash
bun install --frozen-lockfile
bun run build
bun run check
bun test
```

`bun run build` produces the `dist/hosted` and `dist/ksu` bundles. `bun run
check` is `svelte-check --fail-on-warnings`; there is no `typecheck` script.
`bun test` runs the app tests and the same asset guard.

### Packaging

From this firmware repository, verify version pins and then the package target
that changed:

```bash
make version-check
make target_toolkit_android
make target_magisk_module
```

The module recipe stages the app's version-matched `dist/ksu` through
`scripts/stage_ksu.py`; it must not regenerate a divergent UI. The packages also
assert that BDS plus the standalone EFI tools retain byte identity across
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
   that remains. Never claim that a host-side build proves browser or on-device
   behavior.

## Copyright and license

- You retain the copyright to code you create.
- You agree to license and release your contribution under the **GPL license**.
- No Contributor License Agreement (CLA) is required.

## Attribution

If you provide patch ideas, reverse-engineering analysis, or related
contributions in issues, your name may be co-attributed in the corresponding
commit to acknowledge your contribution.
