# Canoe release-target working agreement

## Scope

`targets/` assembles end-user artifacts. Domain behavior belongs in `submodules/` or `tools/`; target Makefiles and resources select, copy, verify, and package it.

- `toolkit_linux/` and `toolkit_windows/`: retired desktop package entrypoints; b4 remains tagged.
- `toolkit_android/`: temporary-root device package and shared device scripts.
- `magisk_module/`: installation/OTA module, WebUI, device scripts, binaries, and bundled `ablrepo` data.
- Every `targets/*/build/` directory is generated output.

## Packaging invariants

- All packages built in one release invocation must carry byte-identical BDS and standalone EFI artifacts.
- A UEFI source edit requires one clean rebuild before packaging. Use top-level `UEFI_REBUILD=1`; never rebuild the non-reproducible EDK II tree independently per package.
- Delete or reject stale package inputs before assembly. Existence alone is insufficient when a prior artifact could survive.
- Package maintained resources and built outputs only. Do not package `.work`, caches, tests, source-only fixtures, absolute local paths, device dumps, or unsigned scratch images.
- Preserve executable bits, required Windows line endings, and pinned asset digests.
- `version.mk` is authoritative; package metadata must pass `make version-check`.
- Supplied user ABL/vbmeta files remain derivation inputs and are not silently copied as flash payloads.

## Device-writing surfaces

Most package assembly is host-only. Installed package scripts are not:

- Magisk customize/OTA flows may write ABL, raw `efisp`, `persist/efisp.fat`, or `vendor_boot`.
- Android temporary-root tooling is documented as boot-root-only; the operator owns raw ABL/BDS writes.
- Hosted managed USB does not mount a host filesystem; its implementation is outside this package tree.

Keep destructive actions behind an explicit operator choice with exact slot, partition, image, and rollback path. Never add device probing or writes to a package build target. Preserve other-slot recovery and transaction rollback when changing install flows.

## Tests and release proof

Run the exact behavior tests for an edited target, then build the artifact:

```sh
sh targets/magisk_module/tests/test_flows.sh
make target_toolkit_android
make target_magisk_module
```

Do not run every target when only one package changed. For release-wide or shared-input changes, build all affected targets in one invocation, using one UEFI rebuild where required.

Inspect each archive's machine-consumed contract: expected paths, executable modes, platform architecture, version, exact sidecars, pinned assets, and byte identity of shared EFI files. Archive creation success is not proof of correct contents.
