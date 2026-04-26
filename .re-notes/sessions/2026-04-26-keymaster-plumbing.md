# 2026-04-26 Keymaster-only plumbing

## Minimal boot result reported by user

- `dist/minimal_generic_superfastboot.efi` booted.
- Android init rescue/reformat path triggered as expected.
- Logs stored under `backups/minimal-generic-superfastboot-efi/`.

## Log review highlights

- `backups/minimal-generic-superfastboot-efi/bootloader_log` shows `KeyMasterSetRotAndBootState` success and HLOS boot.
- Same log includes AVB verification/hash mismatch lines and orange/unlocked-style state observations.
- Some saved logfs entries appear to include older/full patch output, so current bootloader log is preferred for this test outcome.

## New plumbing added

- `KEYMASTER_PATCH` edk2 build define is now forwarded by `edk2/makefile`.
- `edk2/QcomModulePkg/QcomModulePkg.dsc` maps `KEYMASTER_PATCH=1` to:
  - `-DKEYMASTER_PATCH`
  - `-DDISABLE_PATCH_2` through `-DDISABLE_PATCH_9`
- New EFI targets:
  - `make build_keymaster_set_efi`
  - `make fastboot_boot_keymaster_set`
- New Android/Magisk packaging targets:
  - `make build_patcher_android_keymaster`
  - `make magisk_module_keymaster_set`

## Scaffold behavior

- `tools/patchlib.h` now contains `patch_keymaster_request_fields()` under `KEYMASTER_PATCH`.
- Current behavior is intentionally no-op unless a future `ENABLE_KEYMASTER_FIELD_PATCH` implementation is added.
- Runtime message: `Keymaster patch scaffold enabled; request-field mutation is disabled`.

## Verification

- `make build_keymaster_set_efi` succeeded.
- BuildOptions confirmed `KEYMASTER_PATCH: '1'`.
- Compiler invocations include `-DKEYMASTER_PATCH -DDISABLE_PATCH_2 ... -DDISABLE_PATCH_9`.
- Artifact: `dist/keymaster_set_superfastboot.efi`
- Size: `200704`
- SHA256: `db72b243166ab541eb8e4e71d9d90b2cbdb52d2537e527105dd10ba41acb17d0`
- Host syntax check for Android patcher flags passed:
  - `gcc -DKEYMASTER_PATCH -DDISABLE_PATCH_2 ... -DDISABLE_PATCH_9 -fsyntax-only tools/patch_abl.c`

## Next step

- Implement the first real `ENABLE_KEYMASTER_FIELD_PATCH` body for version-binding fields only (`SystemVersion`/`SystemSecurityLevel`) at the Keymaster boot-state request boundary.
