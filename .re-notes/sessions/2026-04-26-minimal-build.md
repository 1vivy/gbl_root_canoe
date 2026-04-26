# 2026-04-26 minimal build verification

## Summary

- Added/validated `MINIMAL_PATCH=1` forwarding into the edk2 build command-line defines.
- `BuildOptions` now records `MINIMAL_PATCH: '1'` with `AUTO_PATCH_ABL: '1'`.
- Compiler invocations for `LinuxLoader` include `-DDISABLE_PATCH_2` through `-DDISABLE_PATCH_9`.
- Gated helpers used only by disabled patches to avoid `-Werror=unused-function` under minimal builds.

## Files changed

- `Makefile`: minimal/fastboot boot artifact targets.
- `edk2/makefile`: forwards `MINIMAL_PATCH` to edk2 `build`.
- `edk2/QcomModulePkg/QcomModulePkg.dsc`: defines `MINIMAL_PATCH` and injects `DISABLE_PATCH_2`…`DISABLE_PATCH_9`.
- `tools/patchlib.h`: guards patch-5-only tracking helpers.
- `tools/arm64_inst_decoder.h`: guards patch-4/patch-5-only encoder helpers.

## Artifact

- Path: `dist/minimal_generic_superfastboot.efi`
- Size: `204800`
- SHA256: `0f654e3389fb526e1ce4ca43904c42438fece5dc2632daae066eeef97f50ce64`

## Confidence

- High that minimal build defines now reach `LinuxLoader` and compile with patches 2–9 disabled.
- Runtime device validation still pending via `fastboot boot dist/minimal_generic_superfastboot.efi`.
