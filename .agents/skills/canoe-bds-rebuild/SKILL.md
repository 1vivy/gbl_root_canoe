---
name: canoe-bds-rebuild
description: "Rebuild Canoe's EDK2 LinuxLoader after QcomModulePkg changes when the canonical Docker compile/link, a stale or missing BDS.efi, or a tolerated inner-build failure must be resolved."
---

# Rebuilding the Canoe BDS

Compile verification for `submodules/uefi/edk2/QcomModulePkg/**` edits in `gbl_root_canoe`. The tree builds with `-Wall -Werror`, so a successful relink is real proof.

## Command

```bash
mkdir -p /tmp/canoe-build        # must be writable by the invoking uid
cd <repo-root>
docker run --rm -v "$(pwd)":/workspace -v /tmp/canoe-build:/out -w /workspace \
  gbl_builder:latest /bin/bash -lc \
  'make -C submodules/uefi build > /out/bds_build.log 2>&1; echo "exit=$?"'
```

- `version.mk` is the single version source. Do not pass `CANOE_VERSION`: a command-line value that differs from `version.mk` is refused (`CANOE_VERSION override refused: canonical …`), and `CANOE_VERSION_OVERRIDE=1` is only for a deliberate non-release build, supplied on the make command line.
- `gbl_builder:latest` (~3 GB) holds the clang/aarch64 toolchain; `run_docker.sh` shows the canonical mount.
- Require the log line `BDS built successfully: build/BDS.efi`; a healthy image is ~700 KB (712704 bytes measured).

## What `make -C submodules/uefi build` does

1. Copies the checked-in `Conf/` into `edk2/`.
2. Runs `embed_variant.py` for both bundled blobs (`blobs/canoe-usbmsd.efi`, `blobs/canoe-managed-msd.efi`) into `Generated/`. A missing blob still builds and falls back to the platform driver.
3. Deletes the whole `edk2/Build/RELEASE_CLANG35` output tree and the previous `build/BDS.efi`, then runs the vendor build **with its status tolerated** (`|| true`): this vendor build has missed header dependencies, so relinking alone can retain incompatible old objects.
4. Requires a newly present `LinuxLoader.efi`, copies it to `build/BDS.efi`, and writes `edk2/Build/.canoe-version`.

## Traps

1. **Never pipe the build into a truncating filter.** `... | sed -n '1,60p'` closes the pipe, SIGPIPEs make, and the run dies mid-compile looking like a fast success. Redirect to a log, then grep the log.
2. **`Build failed` with no compiler diagnostic is the tolerated inner status, not a compile error.** The recipe continues past a failing EDK2 invocation and only then finds no `LinuxLoader.efi`. Read the log for the real error instead of raising timeouts or accepting the binary that was there before.
3. **A warm `edk2/Build` tree with no source change can end with no artifact at all.** The `build` target deletes the output tree first, and EDK2 declines to regenerate a module it considers up to date — so packaging must run `make -C submodules/uefi clean` first, which is what `UEFI_REBUILD=1` does in the root Makefile. Do not reach for `CANOE_VERSION` to force it.
4. Confirm the change actually compiled by object mtime, which survives even a killed run:
   ```bash
   cd submodules/uefi/edk2/Build/RELEASE_CLANG35/AARCH64
   ls -l --time-style=full-iso **/OUTPUT/<File>.obj
   ```
   Object newer than source = it compiled under `-Werror`. Ignore `warning: inconsistent use of MD5 checksums` — every file in this tree emits it.

## After the build

`submodules/uefi/build/BDS.efi` is a gitignored build artifact, not a source. Packaging from the repository root (`make target_magisk_module`, `make target_one_shot_android`) uses `UEFI_REBUILD=1` to clean and rebuild the BDS once per invocation and reuse that output for every package, because the EDK2 build is not byte-reproducible (two clean builds of identical sources were measured to give different sha256) while CI requires byte-identical boot artifacts in every package.

Flashing BDS.efi is a gated operator action — never flash unprompted. Deploying and proving it is `canoe-bds-deploy-verify-ladder`; a blob change needs the producer build first (`canoe-msd-calibration`).
