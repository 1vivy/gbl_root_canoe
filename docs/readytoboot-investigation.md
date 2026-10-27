# Canoe `ReadyToBoot` investigation

Date: 2026-09-12

## Conclusion

`gbl_root_canoe` currently signals `EndOfDxe` and `ReadyToBoot` while starting
its FAT stack, before it loads configuration or enters the boot menu. That
behavior was adapted from Qualcomm's `abl2esp`, but it does not reproduce the
observed OnePlus/Qualcomm ABL chain.

The local Qualcomm platform-BDS reference has a default-app path that launches
`LinuxLoader` without signaling `ReadyToBoot`. The on-slot loader then performs
an OEM-added early handoff to raw `efisp`/GBL with no explicit
`ReadyToBoot`, `EndOfDxe`, or `DetectSdCard` signal found in the inspected
handoff. The final chainloaded ABL shuts firmware down through
`ExitBootServices`, whose event is a normal point at which the reference PMIC
driver performs its cleanup.

The existing Canoe `ReadyToBoot` signal is therefore not a restoration of a
missing step observed in the real chain. It prematurely invokes firmware
callbacks while Canoe may remain in its menu indefinitely.

## Artifacts and analysis identity

- Ghidra project:
  `/home/vivy/Ghidra/projects/canoe-abl-parallel-20260902.gpr`
- On-slot SM8850 ABL selector: `abl_a.LinuxLoader.efi`
- Immutable input:
  `/home/vivy/Ghidra/inputs/canoe-abl-parallel-20260902/abl_a.LinuxLoader.efi`
- SHA-256:
  `2dcc125cfb235e60f522ce2d8c7b8d61e4cc1c9bab2b017e5802717bc2c2cb32`
- Artifact ID:
  `artifact:sha256:2dcc125cfb235e60f522ce2d8c7b8d61e4cc1c9bab2b017e5802717bc2c2cb32`
- Read-only analysis:
  `analysis:2dcc125c:capstone-5.0.7:pe-sections:aarch64-le:rva`
- Reference source:
  `https://git.codelinaro.org/clo/la/abl/tianocore/edk2` at `4d763428ad`
- Qualcomm platform-BDS reference:
  `/home/vivy/Projects/efisp-projects/BOOT.MXF.2.5.1`

The durable Ghidra record still contains an unresolved database-state conflict:
the original analysis recorded 725 functions, while the reopened selector
currently reports zero. The project and both ABL selectors were reopened
read-only. No reanalysis, annotation, mutation, or save was performed. The
control-flow findings below were recovered independently from the immutable PE
with Capstone and matched to the CLO/LA source.

## Platform placement before on-slot ABL

The relevant path in the local Qualcomm platform-BDS reference is
`PlatBdsLaunchDefaultApps()` in:

`BOOT.MXF.2.5.1/buildpath0/boot_images/boot/QcomPkg/Library/PlatformBdsLib/PlatformBdsLib.c`

Its observed source order is:

1. Launch the configured charger application.
2. Read `DefaultBDSBootApp`.
3. Load/process required firmware images.
4. Call `DisplayPOSTTime()`.
5. Update platform variables.
6. Launch the configured ABL application from the authenticated ABL FV with
   `LaunchAppFromGuidedFv(&gEfiAblFvNameGuid, DefaultApp, NULL)`.

There is no `EfiSignalEventReadyToBoot()` or
`EfiSignalEventEndOfDxe()` on this default-app path.

Other BDS paths do signal these events, but they are different paths:

- `BootFromBootOptionList()` signals `ReadyToBoot` before a UEFI boot option.
- `LaunchBDSMenu()` signals `ReadyToBoot`, connects drivers, then signals
  `EndOfDxe` before entering the tools menu.
- Network and debug/Quantum paths have their own explicit signaling.

The device log's:

```text
POST Time      [...] OS Loader
Loader Build Info: Apr 10 2026 19:20:35
```

matches the default-app placement: platform BDS prints the POST marker and then
starts the on-slot `LinuxLoader`.

## On-slot ABL path to `efisp`

The April 10 on-slot ABL corresponds to
`abl_a.LinuxLoader.efi`, not the July 31 managed ABL stored as
`abl_b.LinuxLoader.efi`.

The successful early handoff is inside the OEM-extended
`LinuxLoaderEntry`. PE addresses below are RVAs.

1. `0x4e50`: enter `LinuxLoaderEntry`.
2. `0x4edc-0x5050`: shared-IMEM, unsafe-stack, stack-canary, and boot-stat
   initialization.
3. `0x506c`: locate the MemCardInfo protocol. This is protocol discovery, not
   a `DetectSdCard` event.
4. `0x50a0-0x52e0`: initialize/read Qualcomm `DeviceInfo`, validate
   `ANDROID-BOOT!`, and write defaults when needed.
5. `0x530c-0x5340`: enumerate partitions, update entries, detect multislot
   support, and resolve the active slot.
6. `0x53c4-0x5528`: read OPlus download-state data from `oplusreserve1` and
   process incomplete-download state.
7. `0x5600-0x56b8`: select the literal `efisp` partition, require one matching
   Block I/O handle, calculate its size, and allocate a buffer.
8. `0x56cc` via `0x182a0`: read `efisp` from LBA 0 and reject an all-zero
   leading eight bytes.
9. `0x5ac0`: log `Loading GBL app`.
10. `0x5afc`: call
    `LoadImage(FALSE, parent, NULL, buffer, size, &handle)`.
11. `0x6538`: log `Starting GBL app`.
12. `0x6560`: call `StartImage(handle, &ExitDataSize, NULL)`.

The success edge from `LoadImage` to `StartImage` contains no lifecycle-event
signal.

If GBL returns, execution continues at `0x57d4` into the remaining stock/OEM
loader work, including audio-framework handling, reset-reason processing,
recovery processing, normal Android loading, or fastboot. Those operations are
after GBL in this OEM binary. The older CLO/LA reference `LinuxLoader.c`
independently places reset-reason and recovery processing before its traditional
Android boot path; its inspected version does not contain the OEM
audio-framework handling.

## Event evidence

Raw searches of both preserved ABL PEs found none of these GUID byte sequences:

- `ReadyToBoot`: `7ce88fb3-4bd7-4679-87a8-a8d8dee50d2b`
- `EndOfDxe`: `02ce967a-dd7e-4ffc-9ee7-810cf0470880`
- `DetectSdCard`: `b7972c36-8a4c-4a56-8b02-1159b52d4bfb`

Neither PE contains these GUID literals, and no explicit lifecycle signal was
found in the inspected handoff. This does not exclude indirect signaling
through firmware protocols or externally supplied event handles. It also does
not alone prove that no earlier firmware component signaled them, so runtime
behavior supplies an independent check for the reference PMIC cleanup path.

## Runtime cross-check

The preserved SM8850 boot log at
`gbl_root_canoe/.work/recovery-diagnosis-20260906/mode0-logs/UefiLog1.txt:942`
orders the relevant events as follows:

```text
Loader Build Info: Apr 10 2026 19:20:35
VB: RWDeviceState: Succeed using rpmb!
start block=0x427, size=0x1000
download process is complete.
Loading Image efisp Done : 2 ms, Image size : 3145728 Bytes
Loading GBL app
Starting GBL app
[5192]SFB: MARK stage=fatstack
SFB: MARK fat-connect ... failed=0 status=Success
17 x tz_armv8_smc_call failed ... SmcId = 0x2002201
SFB: MARK event-signal end-of-dxe=Success ready-to-boot=Success
```

`0x02002201` is `TZ_SPMI_REVOKE_ACCESS_ID`: revoke UEFI-owned SPMI/PMIC
channel access at UEFI exit. Qualcomm's PMIC driver registers the same
`PmicExitBootServicesCallback` for both `ReadyToBoot` and
`ExitBootServices`. Its `pm_exit_cleanup()` has a static once-only guard set
before cleanup begins.

Consequently, the 17 revocation attempts when Canoe signals its boot-phase
groups are strong evidence that the reference PMIC `ReadyToBoot` cleanup had
not already run. This inference assumes the shipped PMIC driver matches the
reference implementation and that the burst originates from that callback. It
excludes an earlier invocation of that same guarded cleanup, not every possible
earlier `ReadyToBoot` signal.

The SM8845 log shows the same placement with 28 channels. The difference is a
pattern consistent with platform-specific eligible SPMI channels rather than
Canoe retries. The failure counts alone do not establish each platform's total
channel count.

## Why the `abl2esp` reasoning does not transfer

Qualcomm's `abl2esp` acts as a small boot manager that connects drivers and
then immediately starts `BOOTAA64.EFI`. Signaling a final boot lifecycle event
immediately before its only payload is at least structurally consistent with
that role.

Canoe signals the events inside `SfbStartFatStack()`:

`gbl_root_canoe/submodules/uefi/edk2/QcomModulePkg/Application/LinuxLoader/SuperFbFat.c`

That function runs before configuration loading and before an interactive menu
that can remain active indefinitely. The introducing commit was:

```text
c008636bfa5b6b08ecbf1796a821397fb9c62ce7
Restore bounded FAT boot-root mapping and storage lifecycle
```

Its rationale said that vendor drivers might defer initialization to
`EndOfDxe` or `ReadyToBoot`, and that signaling an unused group had no cost.
The device evidence refutes the no-cost assumption. `ReadyToBoot` invokes
real teardown/provisioning callbacks, including:

- PMIC interrupt/configuration cleanup and SPMI access revocation;
- `XnWrapperLib` replacement of `gBS->AllocatePages`;
- DPP firmware-provisioning work;
- potentially additional callbacks in binary-only DXEs.

None of the inspected storage code establishes a dependency on
`ReadyToBoot`. FAT/EXT4 availability is produced by the separate
`DetectSdCard`, driver installation, and `ConnectController` operations.

## Issue

`SfbStartFatStack()` signals `ReadyToBoot` too early and without a demonstrated
dependency. This prematurely runs firmware shutdown/provisioning callbacks
while Canoe still expects to operate as an interactive boot manager. It also
produces bursts of rejected UEFI SPMI-channel revocations on both tested SoCs.

The combined post-call marker is misleading:

```text
SFB: MARK event-signal end-of-dxe=Success ready-to-boot=Success
```

`SignalEvent()` success only means the event signaling operation succeeded.
Callbacks may be queued, and their internal failures are not reflected in that
status. The reference dispatcher is
`BOOT.MXF.2.5.1/buildpath0/boot_images/edk2/MdeModulePkg/Core/Dxe/Event/Event.c:576`.

## Recommended correction

Remove `SfbSignalBootPhase()` from `SfbStartFatStack()`.

Keep these storage operations conceptually separate:

1. vendor storage-detect signal, if still required by measured targets;
2. FAT/EXT4 driver installation;
3. controller connection;
4. volume discovery and Canoe operation.

Do not move `ReadyToBoot` to another location merely for lifecycle symmetry.
The observed Qualcomm default-app and ABL paths do not establish it as a
required Canoe action. If a future target demonstrates a concrete dependency,
signal it at the narrowest final-handoff boundary and test all callbacks it
activates.

Evaluate `EndOfDxe` separately. Its current coupling to `ReadyToBoot` is not
justified by the evidence, and the observed on-slot ABL handoff does not emit
it either.

## Validation plan for a correction

The removal is a proposed, device-validation-required correction rather than an
already demonstrated safe fix. After removing the boot-phase signal:

- host UEFI tests remain green;
- canonical `gbl_builder:latest` BDS rebuild succeeds;
- `DetectSdCard`, FAT/EXT4 installation, controller connection, container
  mount, configuration loading, menu, and managed launch remain successful;
- `hooks-armed` still reports `installed=4 failed=0 unavailable=0`;
- managed `LoadImage`, handoff publication, and `StartImage` succeed;
- the pre-menu `0x02002201` failure burst disappears;
- Android reaches the same requested locked-mode state;
- a failed/returned child still restores every managed hook.

Device deployment or partition writes require separate explicit authorization.
