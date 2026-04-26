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

## Next RE focus

- Implement a separate Keymaster-only patch set that adjusts RoT, bootstate, and version-binding inputs at the Keymaster request boundary.
- Avoid re-enabling cmdline/device-state/RPMB persistence patches in the minimal boot path.
