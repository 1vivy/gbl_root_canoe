# 2026-04-26 minimal build verification

## Summary

- Added/validated `MINIMAL_PATCH=1` forwarding into the edk2 build command-line defines.
- `BuildOptions` now records `MINIMAL_PATCH: '1'` with `AUTO_PATCH_ABL: '1'`.
- Compiler invocations for `LinuxLoader` include `-DDISABLE_PATCH_2` through `-DDISABLE_PATCH_9`.
- Gated helpers used only by disabled patches to avoid `-Werror=unused-function` under minimal builds.
- Gated the patch driver so a `MINIMAL_PATCH=1` build skips bootstate-chain scanning and LDRB→STRB source tracking when patches 3–5 are all disabled.

## Files changed

- `Makefile`: minimal/fastboot boot artifact targets.
- `edk2/makefile`: forwards `MINIMAL_PATCH` to edk2 `build`.
- `edk2/QcomModulePkg/QcomModulePkg.dsc`: defines `MINIMAL_PATCH` and injects `DISABLE_PATCH_2`…`DISABLE_PATCH_9`.
- `tools/patchlib.h`: guards patch-5-only tracking helpers.
- `tools/arm64_inst_decoder.h`: guards patch-4/patch-5-only encoder helpers.

## Artifact

- Path: `dist/minimal_generic_superfastboot.efi`
- Size: `200704`
- SHA256: `09351f8dd53d4162e71c4fac4f3240a5b1f13531895b53a2cbdfb6f0127c1d90`

## Confidence

- High that minimal build defines now reach `LinuxLoader`, compile with patches 2–9 disabled, and skip runtime patch-driver work for patches 3–5.
- Runtime device validation still pending via `fastboot boot dist/minimal_generic_superfastboot.efi`.
