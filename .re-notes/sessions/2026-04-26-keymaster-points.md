# 2026-04-26 Keymaster RoT/bootstate patch points

## Ghidra updates

- Program: `LinuxLoader.efi`
- Saved: yes
- Added decompiler comments at:
  - `00023edc` — RoT digest preparation / future Keymaster-only patch candidate.
  - `00023f34` — `KEYMASTER_SET_ROT` request population/send boundary.
  - `00023f9c` — `KEYMASTER_SET_BOOT_STATE` request setup region.
  - `000240c8` — Keymaster sequence completion before VBH path.

## Source reference map

- `external/edk2-uefi.lnx.5.0.r10-rel/QcomModulePkg/Library/avb/VerifiedBoot.c:1738-1758` — builds the Keymaster `Data`, obtains version/security-patch values, then calls `KeyMasterSetRotAndBootState()` and `SetVerifiedBootHash()`.
- `external/edk2-uefi.lnx.5.0.r10-rel/QcomModulePkg/Library/avb/VerifiedBoot.c:1222-1257` — `GetOsVerAndSecPactchLevel()` version-binding source.
- `external/edk2-uefi.lnx.5.0.r10-rel/QcomModulePkg/Library/avb/KeymasterClient.c:338-465` — reference implementation of `KeyMasterSetRotAndBootState()` request construction.
- `external/edk2-uefi.lnx.5.0.r10-rel/QcomModulePkg/Library/avb/KeymasterClient.h:43-82` — `KMRotAndBootState`, `KMBootState`, `KMSetBootStateReq` layout.

## Current design decision

- Keep minimal boot artifact separate from Keymaster experiment.
- Future patch should target the Keymaster request boundary, not `androidboot.*` cmdline construction, RPMB lock state, or AVB verification result handling.
- Version binding can be satisfied by controlling the `SystemVersion`/`SystemSecurityLevel` fields used in the `KEYMASTER_SET_BOOT_STATE` request, rather than weakening rollback-index checks.

## Next implementation step

- Add a distinct `KEYMASTER_PATCH=1`/`build_keymaster_set_efi` path that can enable a new Keymaster-only patch while leaving patches 2, 4, 5, 6, 7, 8, and 9 disabled.
- Start with hard-coded request-field patching for proof-of-life, then replace constants with GBL-provided volatile configuration.
