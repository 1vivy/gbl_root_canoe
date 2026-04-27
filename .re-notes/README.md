# Reverse-engineering notes

## Current target

- Program: extracted OEM `LinuxLoader.efi` / integrated ABL LinuxLoader path for canoe.
- Ghidra project/session: `gbl_root_canoe` via GhidraMCP GUI bridge.
- Primary goal: identify safe patch points for Keymaster RoT/bootstate control while keeping AVB/cmdline/RPMB-visible device state untouched unless explicitly enabled.

## Key confirmed anchors

- `000290c4` — `AvbSlotVerifyAndBuildCmdline`.
- `00021f08` — `LoadImageAndAuthVB2_KeymasterFlow`.
- `00023edc`–`000240c8` — Keymaster request/send region.
- `00029de0` — `androidboot.vbmeta.device_state` command-line construction xref.
- `00019064` — `Sending Milestone Call` xref.

## Build/patching state

- Minimal fastboot EFI path is intended to keep only patch 1: UTF-16 `efisp` → `nulls`, preventing EFISP recursion.
- Patches 2–9 are disabled for the minimal artifact via `MINIMAL_PATCH=1` and compiler defines `DISABLE_PATCH_2`…`DISABLE_PATCH_9`.
- Verified artifact: `dist/minimal_generic_superfastboot.efi`, SHA256 `09351f8dd53d4162e71c4fac4f3240a5b1f13531895b53a2cbdfb6f0127c1d90`, size `200704` bytes.
- Patch behavior reference: `sessions/2026-04-27-patchlib-patches-1-6.md` summarizes what `tools/patchlib.h` patches 1-6 do and their code evidence.

## Status (2026-04-27, v13: resolved)

- Hook-only path produces clean OEM-side RKP + key attestation. Device-side OEM test menu confirms.
- Working artifact: `dist/keymaster_set_superfastboot.efi`, SHA256 `8ec51959c893bfec294b3626557d86bd75f897c39feafe60d1ff287aa1fb45e0`, size `208896` bytes (`make build_keymaster_set_efi`).
- Active mutations: QSEECOM hook spoofs SET_ROT (0x201) + SET_BOOT_STATE (0x208) to GREEN/locked; SET_VBH (0x211) passthrough; DeviceInfo READ passthrough; DeviceInfo WRITE forces `is_unlocked=TRUE` + `is_unlock_critical=TRUE`; SCM hook drops `ScmSipSysCall(SmcId=0x02000801=TZ_BLOW_SW_FUSE_ID)` for both `TZ_HLOS_IMG_TAMPER_FUSE` (FuseId=0) and `TZ_HLOS_TAMPER_NOTIFY_FUSE` (FuseId=23).
- Side effect: RPMB now persists `is_unlocked=TRUE` indefinitely — ABL UI / cmdline / `verifiedbootstate` continues to report orange to userspace. TZ KeyMint sees clean attestation regardless.
- Patches 2–9 in `tools/patchlib.h` are dead weight in the KM build (already disabled by `KEYMASTER_PATCH=1` via `QcomModulePkg.dsc:196`); they remain only for the legacy `build_generic` / `build` paths.

## Open follow-ups

- Verify post-SCM-hook 0x219 UDS/FRS bytes from a boot log directly (v13 log truncated at `dropping SetFuse(23)`; OEM test menu is current ground truth). LOGFS 5MB cap + `/proc/bootloader_log` truncation punted per user direction.
- Userdata FBE/metadata decryption sanity check: `is_unlocked=TRUE` persisted in RPMB across boots is now stable, but confirm vold doesn't enter Rescue Party once actual ROM is flashed.
- Patches 2–9 in `tools/patchlib.h` are dead in the hook flow but still compile into the legacy `make build` / `make build_generic` paths. Remove once the hook flow is shipped on real custom ROMs.
- TZ rollback / anti-rollback / version-set SmcIds beyond `TZ_BLOW_SW_FUSE_ID` — `KM_BLOCK_TZ_ROLLBACK` scaffold in `tools/scm_hook.h` is in place but the SmcId list is not populated. See `sessions/2026-04-27-hook-refactor.md` for the audit plan.
- Magisk module variant (`magisk_module_hooks`) that runs `tools/ota_to_overrides.py` against the inactive-slot vbmeta and stages the resulting EFI. Per user: inactive-slot only, warn that OTA must be fresh.

## Build flow (post-refactor)

- `make build_hooks_generic` — runtime path. `dist/hooks_generic.efi`. ABL extracted from partition + patched + hooked at boot.
- `make build_hooks` — static path. `dist/hooks_static.efi`. Needs `images/abl.img`.
- `make build_keymaster_set_efi` — deprecated alias for `build_hooks_generic`, kept while older session notes still reference it.
- Single build flag: `ENABLE_KEYMASTER_HOOKS=1`. Auto-disables patches 2-9. Compiles in `tools/qseecom_hook.h` + `tools/scm_hook.h`. Old `KEYMASTER_PATCH` / `KEYMASTER_FIELD_PATCH` split was collapsed during the 2026-04-27 refactor — see `sessions/2026-04-27-hook-refactor.md`.
- Per-OTA constants: `python3 tools/ota_to_overrides.py /path/to/vbmeta.img` writes `tools/keymaster_overrides.generated.h` (gitignored, picked up via `__has_include`). Without it, the lab fallback constants in `tools/keymaster_overrides.h` are used.

## Latest Keymaster protocol-hook result (v6)

- Logs: `backups/minimal-keymaster_set_superfastboot-efi-v6/`.
- Protocol hook succeeded: `QseecomSendCmd` and SPSS `ShareKeyMintInfo` were hooked.
- SET_ROT was changed from ORANGE/unlocked digest `SHA256(0x01)` to GREEN/locked digest `44149b5d...18e566c7` and TZ accepted it.
- SET_BOOT_STATE was changed from `IsUnlocked=1, PublicKey=0, Color=1` to `IsUnlocked=0, PublicKey=44149b5d..., Color=0`; TZ accepted it.
- SET_VBH was passed through unchanged: `84dcf3542b0613ae33daf635a7c6ed7a7d01d5a5272cf42515d6afbcfea393bc`.
- SPSS shared struct was overwritten consistently with the same modified RoT/BootState and original VBH.
- KeyAttestation reportedly looks valid/good, so the Keymaster-facing spoof succeeded.
- Bootloader still reports AVB/cmdline as orange (`VB2: boot state: orange(1)`, `androidboot.verifiedbootstate=orange`), producing a mixed state: Android-visible AVB is orange while Keymaster attestation says locked/verified.
- Hypothesis: this mixed state can invalidate Android FBE/metadata-decryption expectations. AOSP init/vold derive or unwrap disk/metadata keys through KeyMint/Keymaster using boot parameters and rollback/security state. Changing Keymaster's verified boot parameters makes existing encrypted userdata/metadata keys appear bound to a different boot state, so vold/init may fail decryption and trigger Rescue Party/recovery/wipe paths. This is consistent with success in attestation but storage/recovery fallout.

## DICE/BCC/pvmfw follow-up

- External Qualcomm edk2 has a real pvmfw BCC flow: `PopulateBccParams()` -> `KeyMasterGetFRSAndUDS()` -> pvmfw code/authority hashes -> `GetBccArtifacts()` -> `AppendPvmFwConfig()`.
- This path likely feeds AVF/pVM DICE handoff, not Qualcomm/default KeyMint RKP or Widevine directly.
- v6 RKP/Widevine failures occur at Android runtime in Qualcomm secure-world signing/CSR generation (`RpcGenerateCertificateV2Request failed`, Widevine `GENERATE_SIGNATURE_ERROR`), after ABL hooks are gone.
- Best next RE target is runtime qti-keymint/Widevine QSEE/QTEE command tracing, plus optional ABL logging of `KEYMINT_GENERATE_FRS_AND_UDS` to rule out pvmfw BCC impact.

## 2026-04-27 VerifiedBoot device-state hook / BCC mode patch

- Implemented in `tools/qseecom_hook.h` and built as `dist/keymaster_set_superfastboot.efi`.
- Artifact SHA256: `3ddf585d97882143778a3efb173d0268d5351844cb90eb94712906b3bd75403b`, size `208896` bytes.
- New hook covers `QCOM_VERIFIEDBOOT_PROTOCOL.VBRwDeviceState`:
  - Logs `READ_CONFIG` / `WRITE_CONFIG` `DeviceInfo` summaries.
  - Forces `DeviceInfo.is_unlocked=FALSE` and `is_unlock_critical=FALSE` on `WRITE_CONFIG` before it reaches the real VerifiedBoot protocol.
  - Forces the successful `READ_CONFIG` result buffer to unlocked for ABL consumers, matching the Superturtles-style read-unlocked/write-locked policy.
- New hook covers `QCOM_VERIFIEDBOOT_PROTOCOL.VBSendMilestone` and QSEECOM CmdIds `0x202`/`0x203`/`0x204` with bounded dumps.
- Latest safety cleanup:
  - Keymaster QSEECOM mutation/capture is gated on the tracked `keymaster` app handle.
  - `DeviceInfo` mutation requires `DEVICE_MAGIC`.
  - Delayed BCC patch scans only the actual child image range recorded after `LoadImage()` and validates the candidate looks like `BccParams_t` with `componentName="pvmfw"`.
  - The BCC patch only changes `Mode` from debug (`2`) to maintenance (`3`) and zeroizes captured UDS/FRS after the scan attempt.
- Ghidra bookmarks added at `00019064` and `00021f08` under `KM hook notes`; program saved.
- v7 boot log (`backups/bootloader_log_v7`) result:
  - Hook install and actual child-image scan range worked.
  - Read-unlocked/write-locked policy worked and RPMB `WRITE_CONFIG` returned success.
  - `KEYMINT_GENERATE_FRS_AND_UDS` still returned dummy sentinels (`UDS[0..3]=0x02020202`, `FRS[0..3]=0x01010101`).
  - BCC mode patch found only a rejected non-`BccParams_t` match (`mode=0`) and did not patch.
  - Therefore the remaining RKP/Widevine blocker is likely secure-world/RPMB lifecycle policy or another KeyMint/Oplus state path, not the basic ABL `DeviceInfo.is_unlocked` read/write path.
- SPSSLib follow-up:
  - BOOT.MXF.2.5.1 `SPSSLib/keymint_info.c` shares only `RootOfTrust`, `BootInfo`, and `Vbh` with SPU via a `0xFADEC0DE` PIL-region blob plus SHA-256 hash in WONCE registers.
  - No direct FRS/UDS/BCC fields are present in that bridge, so it explains SPU boot-context sharing but not the dummy `0x219` UDS/FRS by itself.
  - Latest artifact adds safe `0x219` probe logging for request/response `FrsSec` summaries and UDS/FRS repeated-byte patterns. SHA256: `97ebe2a6058f659a9a54724501af2e62d6acecacfb0d7433ce079a229f87e0b2`.
- v8 boot log (`backups/bootloader_log_v8`) result:
  - KeyMint `0x219` request had `FDR_FLAG=0`, `FrsSecLen=32`, and FrsSec was repeated `0x0F`.
  - Response echoed the same repeated-`0x0F` FrsSec and still returned sentinel UDS/FRS (`0x02` and `0x01` repeated).
  - This narrows the issue to KeyMint/secure-world FRS/UDS policy/input handling; next safest step is instrumenting the `0x202` pointer buffer before active mutations.
- v9 transient FDR probe artifact:
  - Logs indirect `0x202`/`0x203` pointer buffers and DeviceInfo summaries.
  - Transiently sends factory-style `0x219` input (`FdrFlag=1`, `FrsSecLen=0`, zeroed request FrsSec).
  - Suppresses the following `WRITE_CONFIG` persistence while returning success, so probe state should not be written to RPMB.
  - Artifact SHA256: `58ea6c58fa1e3f22c0a92da4cf659d3f23acb2babf1888c3c718b5f3a66fd4da`.
- v9 boot log (`backups/bootloader_log_v9`) result:
  - Indirect `0x202` buffer confirmed secure-side/read buffer still has `FdrFlag=0`, `FrsSecLen=32`, repeated-`0x0F` FrsSec.
  - The hook changed the `0x219` request to `FdrFlag=1`, `FrsSecLen=0`, but KeyMint still returned sentinel UDS/FRS (`0x02`/`0x01`) and response FrsSec repeated `0x0F`.
  - The probe suppressed write persistence. The captured log did not include milestone/ExitBootServices after the probe and contains a new UEFI-start fragment, but user confirmed the device booted into Android system; treat this as log wrap/truncation/malformed concatenation, not reset proof.
  - Next likely probe: make the transient FDR state consistent earlier by patching the DeviceInfo READ result itself, not just the `0x219` request.
- v9 Android result:
  - `remote_provisioning csr default` still fails with `ServiceSpecificException code 1`.
  - `remote_provisioning certify default` still fails with `RkpProxyException: ERROR_UNKNOWN`.
  - Widevine provisioning still fails in `rkpdapp`: `getProvisionRequest`, `cdm_err=323`, `error_code=13`, `error_context=14`, `oem_err=1`.
- v10 DeviceInfo FDR-read probe artifact:
  - Built as `dist/keymaster_set_superfastboot.efi`, SHA256 `bc7fb73528ef53e3b220e5792b9bed8972fab9bdb9ddceaff99f0600c18ae5b7`.
  - Changes the factory-FDR probe from direct `0x219` request mutation to a consistent earlier `DeviceInfo` READ mutation: after `ShareKeyMintInfo`, the next valid `READ_CONFIG` result is transiently shaped as `FdrFlag=1`, `FrsSecLen=0`, zeroed `FrsSec`, then regular read-unlocked behavior still applies.
  - `0x219` now only verifies/logs whether ABL naturally built a factory-style request from the patched DeviceInfo.
  - The following active-probe `WRITE_CONFIG` remains suppressed to avoid persisting factory-FDR state to RPMB.
  - Boot-test pass/fail markers: look for `FDR DeviceInfo probe armed`, `forced factory-FDR for probe`, `FDR DeviceInfo probe produced factory-style 0x219 request`, non-sentinel UDS/FRS, and `FDR probe suppressing WRITE_CONFIG persistence`.
- v11 real-FDR-path artifact:
  - Built as `dist/keymaster_set_superfastboot.efi`, SHA256 `bc319463a4f9333374eac2d90df492b131d62d7e8e3e8077417ae3f9bedb954a`.
  - Default build disables synthetic FDR DeviceInfo spoofing (`KM_ENABLE_FDR_TRANSIENT_PROBE=0`) and disables FDR WRITE suppression (`KM_SUPPRESS_FDR_PROBE_WRITE=0`).
  - Intended for testing the OEM path: misc recovery message -> `BootIntoRecovery` -> `DetectFDR()` -> `SetFDRFlag()` persists `FdrFlag=1`, `FrsSecLen=0`, zeroed `FrsSec`, then KeyMint `0x219` should consume that real persisted state and ABL should write back `FdrFlag=0` plus returned `FrsSec`.
  - Real-FDR log expectations: no `FDR DeviceInfo probe armed`; no `FDR probe suppressing WRITE_CONFIG`; look instead for `LinuxloaderEntry: FDRDetected`, a forwarded `WRITE_CONFIG` carrying `FdrFlag=1 FrsSecLen=0`, `0x219 FDR_FLAG=1 FrsSecLen=0`, and a later forwarded `WRITE_CONFIG` clearing/persisting the returned FRS secret.
- v12 SET_VBH zero-digest probe (`backups/bootloader_log_v12`):
  - Built with `KM_VBH_PROBE_ZERO=1`. SET_VBH branch overwrites the 32 Vbh bytes with zero (CmdId preserved).
  - Result: `KM hook: SET_VBH response Status=0 (SendCmd ret=Success)` → TZ accepted all-zero VBH; 0x219 still returned `UDS=0x02 repeated`, `FRS=0x01 repeated`.
  - Conclusion: TZ does not validate SET_VBH content. The `KM_OVERRIDE_VBH` constant idea is dead; SET_VBH stays passthrough.
- v13 SCM hook + write-unlocked DeviceInfo policy (`backups/bootloader_log_v13`, OEM test menu confirms clean RKP + key attestation):
  - Built as `dist/keymaster_set_superfastboot.efi`, SHA256 `8ec51959c893bfec294b3626557d86bd75f897c39feafe60d1ff287aa1fb45e0`, size `208896`.
  - Flipped DeviceInfo policy: READ passthrough (was: force unlocked-for-ABL); WRITE forces `is_unlocked=TRUE` + `is_unlock_critical=TRUE` (was: forced locked). RPMB now persists unlocked.
  - New `tools/scm_hook.h` installs on `gQcomScmProtocolGuid.ScmSipSysCall`. Drops `SmcId == 0x02000801` (`TZ_BLOW_SW_FUSE_ID`) and returns `EFI_SUCCESS` without forwarding. Build flag `KM_BLOCK_TAMPER_FUSE` (default 1) controls drop vs log-only.
  - `LinuxLoader.inf` Protocols list extended with `gQcomScmProtocolGuid` (linker fails without it).
  - Log evidence (lines 1030-1033): child ABL fired `SetFuse(0)` and `SetFuse(23)`; SCM hook intercepted both. This was the missing path divergence vs binary-patches-1-6.
  - Mechanism: ABL-local `BootState->Color` is decided by `IsUnlocked()` *before* the QSEECOM call boundary the hook intercepts at. With write-unlocked + read-passthrough, ABL natively sees unlocked → `Color=ORANGE` locally → `KeyMasterSetRotAndBootState` (`KeymasterClient.c:467-486` upstream) fires `SetFuse(TZ_HLOS_IMG_TAMPER_FUSE)` + `SetFuse(TZ_HLOS_TAMPER_NOTIFY_FUSE)` via `ScmSipSysCall` — a separate protocol the QSEECOM hook can't see. Once those fuses are blown, TZ KeyMint emits sentinel UDS/FRS at 0x219 (`0x02` = `kDiceModeDebug`, `0x01` = `kDiceModeNormal`) instead of real device-unique secrets. Binary-patches-1-6 path natively produced `Color=GREEN`, never tripped this gate.
  - Note: v13 log truncated at `dropping SetFuse(23)` so direct in-bootloader UDS/FRS readout for this boot is not captured; OEM device-side test menu is the ground truth.
