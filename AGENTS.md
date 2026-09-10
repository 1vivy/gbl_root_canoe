# GBL Root Canoe working agreement

## Runtime model

This repository owns the post-authentication Canoe path:

```text
signed vulnerable ABL -> raw efisp:BDS.efi -> persist/efisp.fat mounted boot root -> managed loader -> Android
```

The raw `efisp` partition is not a filesystem. It contains `BDS.efi` as a whole-partition image loaded by the vulnerable, already authenticated ABL. `persist/efisp.fat` is a FAT16 container inside ext4 persist, sized in 8 MiB increments from 8 through 256 MiB. The running BDS advertises this range separately; older `fat16-container-v1` firmware supports only its 32 MiB geometry. Legacy `/persist/efisp` is ignored and never imported.

`BDS.efi` is the EDK II `LinuxLoader` UEFI application. It runs in the inherited non-secure EL1 UEFI environment with XBL/ABL Boot Services alive. This is arbitrary NS-EL1 code execution, not ownership of EL2, EL3, TrustZone, XPU-protected memory, or unrestricted SMCs. The final chainloaded ABL/kernel owns `ExitBootServices`; the Canoe BDS does not call it.

## Repository map

- `submodules/uefi/`: owner BDS and standalone EFI tools. Mutable code is primarily `edk2/QcomModulePkg` and `edk2/AndroidToolsPkg`; most other EDK II content is vendor/upstream support.
- `submodules/patcher/`: ABL patching engine.
- `submodules/ablfvextractor/`: ABL FV extractor.
- `tools/canoe-bootmgr/`: small mounted-root CLI/library for config, entries, BLS and prepared loaders. No device discovery, deployment assessment, GUI sessions, or automatic snapshot/readback workflows.
- The hosted browser app and the KernelSU WebUI live in the sibling `canoe-boot-manager`. Hosted WASM replaces desktop sidecars. Android retains its native root worker at `native/canoe-manager`. Package only explicit version-matched `CANOE_KSU_DIST` assets; b5 must never fetch the b4 WebUI archive.
- `tools/canoe-ext4/`: historical native ext4 helper retained for reference; absent from b5 release and default test paths. Browser ext4 is owned by the Rust dependency fork and `canoe-nusb-storage`.
- `tools/canoe-host/`: retired. Unreachable from every shipped path; the Rust `canoe` CLI and the app replaced it. Do not revive it.
- `tools/mode2-profile/`, `tools/abl-tzmap/`: Rust sidecar derivation.
- `targets/`: end-user package assembly; `targets/*/build/` is generated.
- `ablrepo/`: intentionally tracked vulnerable ABL artifacts and metadata, not general build output.
- `wiki/`: user/operator documentation.
- `dev_targets/`: development-only workflows, never release defaults.
- `.work/`, `.cache/`, `.ruff_cache/`, EDK II `Build/`/`Conf/`, Rust `target/`, logs, extracted trees, and ordinary `.efi`/`.img`/`.bin` files: local artifacts unless explicitly unignored.

`../gbl-chainload` is a separate upstream/reference project, not a build dependency. Port behavior deliberately; never edit it as a hidden second implementation of Canoe.

## Load-bearing contracts

- BDS owns its bounded FAT mapping and firmware mount. Explicit default, managed-entry mode and boot-policy saves may persist config; ordinary selections remain one-shot. Application workflows own deployment snapshots/readback/recovery; command primitives retain input validation and checked I/O.
- `canoe.cfg` is the canonical persisted configuration. Ordinary entry launches do not change it; the Advanced preference actions save only their named setting. Its boot policy is `menu-mode silent|menu` with `key-window` milliseconds and a `menu-timeout` that only counts down in menu mode; `timeout` is a pre-b2 reader alias no writer emits. `show-booting yes|no` controls the launch banner and defaults to yes. A `default` names a config entry id or a discovered BLS row as `bls:<stem>`, and an unresolvable default opens the menu instead of launching another row.
- Every managed `boot_a.efi`, `boot_b.efi` or `boot_backup.efi` generation carries its matching 120-byte `.gm2p` and 256-byte `.tzmap` sidecars. `boot.efi` is a pre-b2 compatibility name the BDS still reads.
- Prepared managed ABL images disable their efisp lookup through the ABL patcher (`efisp` → `nulls`). There is no runtime efisp Block I/O hiding hook. Do not assume unmodified on-slot ABL can be chainloaded safely.
- Mode 0 remains honest-unlocked. Mode 1/2 policy hooks must be scoped to the one managed child lifecycle.
- Every installed protocol or Block I/O wrapper must restore on failed preparation, failed `LoadImage`, child return, mode change, and fastboot/menu re-entry.
- A signer digest change is evidence of a different signer, not proof of OEM identity. Preserve the explicit override boundary.
- `version.mk` is the single version source, and `make bump` regenerates it wholesale: never hand-edit it or the files it generates, and never keep build paths there. Run `make version-check` after version work. The KSU dist manifest must match product, version and runtime before packaging.

## Implementation discipline

Fix behavior in its authoritative layer:

- Boot/menu/hook behavior: `submodules/uefi/edk2/QcomModulePkg/Application/LinuxLoader`.
- ABL byte patching: `submodules/patcher`.
- Config grammar and mounted boot-root primitives: `tools/canoe-bootmgr`. Share these primitives; slot/OTA workflow policy belongs to the manager application.
- Browser ext4/FAT storage: the sibling Rust dependencies and `canoe-nusb-storage`. Android persist allocation uses its existing mount. Do not restore native desktop mounting or libext2fs helpers.
- Browser USB/storage orchestration: manager runtime and `canoe-nusb-storage`, outside the small CLI.
- Desktop/WebUI presentation and guided deployment: the `canoe-boot-manager` repository. The KSU installer only deploys the manager package; the activated WebUI owns partition workflows.
- Packaging only: `targets`; do not duplicate domain logic into package Makefiles.

Declare a symbol used across files in the shared header, never hand-copied into a `.c`; and let the canonical build arbitrate include order and toolchain behaviour, not an editor's language server.

Host, device and GUI surfaces no longer need behavioral alignment work: they share one implementation through the `canoe-bootmgr` protocol, so a divergence is a bug in a caller rather than a contract to maintain twice. Preserve input images: supplied ABL/vbmeta files are derivation inputs, never implicit flash payloads.

## Verification

Run the narrow contract first, then the relevant aggregate:

- UEFI host contracts: `make -C submodules/uefi test`.
- UEFI compile/relink after any `QcomModulePkg` C/H/INF/DSC/FDF change: canonical `gbl_builder:latest` build from the `canoe-bds-rebuild` workflow; require `submodules/uefi/build/BDS.efi` from the current sources.
- Patcher: `make -C submodules/patcher test`.
- App surfaces (separate repo `canoe-boot-manager`): `bun run typecheck` must report 0 errors AND 0 warnings, then `bun test`, then `bun run build` including its asset guard.
- Rust tools: `cargo test --locked --manifest-path` for `tools/canoe-bootmgr/Cargo.toml`, `tools/mode2-profile/Cargo.toml`, `tools/abl-tzmap/Cargo.toml`.
- Userspace ext4 helper: `make -C tools/canoe-ext4 test`; set `CANOE_EXT4_WINDOWS_BIN` to also exercise the Windows binary under Wine.
- Shipped Windows binaries: `x86_64-w64-mingw32-objdump -p <exe> | grep 'DLL Name'` must list only system DLLs; a MinGW runtime import is a shipping bug.
- Device/module flows: the exact shell tests named by the top-level `make test` target.
- Cross-project mass-storage changes: rebuild and verify in `../canoe-msd`, copy the verified blob, then rebuild BDS.

For release-package changes, run the affected target build and inspect archive contents. A green unit suite does not prove that the package contains current or byte-identical EFI artifacts.

## Device and release safety

- Builds and tests must not flash or write device partitions.
- Flashing ABL or raw `efisp`, writing `persist`, changing slots, patching `vendor_boot`, and module install/OTA actions require explicit authorization at the point of action.
- Do not use a live `persist` mass-storage export concurrently with Android; host writes can corrupt the filesystem.
- Preserve rollback paths and the other-slot recovery story for every device-writing change.
- Never commit or push unless explicitly requested. Keep device dumps, boot logs, extracted firmware, scratch binaries, and worktrees under ignored artifact paths.
