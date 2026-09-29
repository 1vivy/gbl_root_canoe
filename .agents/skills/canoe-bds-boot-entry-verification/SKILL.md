---
name: canoe-bds-boot-entry-verification
description: "Verify a Canoe BDS canoe.cfg boot entry or harvest that session's evidence when launch, LoadOptions byte-exactness, path resolution, logfs bds-N.log rotation, or the vendor UefiLog is in doubt."
---

# Canoe BDS boot-entry verification and session evidence

Two jobs in one session: prove a hand-written `canoe.cfg` row enumerates and launches with byte-exact `LoadOptions` without side effects, and get the evidence out (Canoe's own `bds-N.log`, the kernel ring, or the vendor `UefiLog`). Both work on OnePlus 15 / SM8850; only the log half needs no kernel handoff.

## Boot-root facts (7.0.7)

- The boot root is the FAT16 container `persist/efisp.fat` (8–256 MiB in 8 MiB steps; original 32 MiB containers still mount). The container filesystem root **is** the boot root, so `image tools/X.efi` resolves to `\tools\X.efi`. Do not add the legacy `\efisp` prefix; legacy `persist/efisp` is ignored and never imported.
- `canoe.cfg` lives at `<mount>/canoe.cfg` (BDS: `\canoe.cfg`). Global keys precede the first `entry`: `version 1`, `generation`, `menu-mode silent|menu`, `key-window 500..5000` (ms, default 1200, legacy values clamp on read), `menu-timeout 0..300` (counts down only in menu mode), `show-booting yes|no`, `fastbootd-mode2 yes|no`, `default`, `mode 0|1|2`, `devinfo-repair asneeded|never`.
- Entry keys: `title`, `image` xor `action fastboot`, `options`, `mode`, `role`. `options` is the UEFI LoadOptions, at most 383 characters, handed over byte for byte — `/` is never folded, and an empty `options` is a rejected line.
- `timeout N` is only a pre-b2 compatibility alias for `menu-mode menu` + `menu-timeout N`; no current writer emits it.
- Silent startup opens the menu only for **VOL UP** inside `key-window`; VOL DOWN and Power do not interrupt it. Menu mode opens the menu immediately and counts down, and any key press cancels that countdown — only Power/Enter confirms a row. An unresolvable default opens the menu instead of launching another row.

## Procedure

1. Validate the config offline with the production parser before touching the device:

   ```bash
   cc -std=c11 -DSFB_HOST_BUILD -I<LinuxLoader> -o cfgcheck cfgcheck.c <LinuxLoader>/SuperFbConfig.c
   ```

   Require `valid=1`, `rejected=0`, and the exact `Options` string. `make -C submodules/uefi test` covers the same parser as `test_config`.

2. Stage a probe loader under the boot root (`tools/`) and add a row that names it while its payload path stays absent at the volume root:

   ```text
   entry probe-prefix
     title Stage A - loader probe (no jump)
     image tools/FdLoader.efi
     options \mu\Payload.fd 0xBASE 0xSIZE
   ```

   `FdLoader` is the fixture the measured Stage A run used; it ships in no repository, so stage it or an equivalent loader that prints usage and reads its `options` path. The launched loader resolves that path against the same volume root the BDS used — the container's filesystem root, never a parent — and reads before reserve/copy/`ExitBootServices`, so `Not Found` is side-effect-free.

3. Enter the flashed BDS. `adb reboot recovery` first, then hold **VOL UP** through the reboot: the pending recovery reason keeps adb available afterwards, and VOL UP opens the menu inside the configured `key-window`.

4. RAM-boot the candidate, or stay on the flashed efisp for a flashed-only proof:

   ```bash
   fastboot boot <BDS.efi>     # Canoe Super Fastboot wraps the raw PE; no Android boot image needed
   ```

   `fastboot oem boot-efi` is not implemented by the current OEM handler.

5. Run the probe row, let it return to the menu, then select the Android row. The untouched pending reason carries the device into recovery, where adb is immediately available.

## Harvesting the evidence

6. Kernel ring, only when the session reached a kernel:

   ```bash
   adb shell grep -E 'SFB: MARK (launch|image-)' /proc/bootloader_log
   ```

7. Canoe's own log file over adb:

   ```bash
   adb shell 'mkdir -p /tmp/lf && mount -o ro -t vfat /dev/block/by-name/logfs /tmp/lf'
   adb shell ls -la /tmp/lf/canoe/
   adb pull /tmp/lf/canoe/bds-1.log /tmp/
   adb shell 'umount /tmp/lf'
   ```

   `/tmp` is fresh tmpfs after reboot, so `mkdir -p` is required.

8. Or export over USB, which needs no OS handoff at all:

   ```bash
   fastboot getvar canoe-bds           # confirm identity before the export removes fastboot
   fastboot oem mass-storage:logfs     # targets are boot-root, persist, logfs
   sudo mount -t vfat -o ro /dev/sdX /mnt/x
   cp /mnt/x/canoe/bds-*.log /tmp/; cp /mnt/x/UefiLog*.txt /tmp/ 2>/dev/null
   sudo umount /mnt/x
   ```

   End the export with **Volume Down on the device**. The gadget fully replaces fastboot (`fastboot devices` is empty) and the export is not an ejectable device: the removable bit follows the LUN, so the boot-root container presents as removable media while a raw `persist`/`logfs` export keeps fixed-media identity; do not send SCSI `START STOP UNIT`. The same export also carries the standalone tools' own dumps at the volume root — `SurfaceTools.log`, `SurfacePolicy.log`, and `UsbToolsDump.txt`. The first fastboot command after gadget re-enumeration may answer `< waiting for any device >`; retry once.

## Reading the files

Two writers produce files on logfs. Qualcomm's `WriteLogBufToPartition` runs only at ExitBootServices, reset notification, capsule reset or FwProvision, and retail firmware may block mounting via `IsDebugPartition` while leaving `WriteFile`/`OpenFile` ungated. Canoe's `SfbMountLogfs`/`SfbOpenLogfsRoot` flush is independent and survives sessions that never reach an OS.

Canoe's capture, `\canoe\bds-N.log` (N = 0..2):

| Evidence | Healthy value |
| --- | --- |
| Header | `Canoe BDS session; seq=N; tag=...; captured-bytes=N`, N in low thousands |
| Ring order | `[canoe capture; oldest first]`; no drop notice unless overflowed |
| Pre-launch tail | `SFB: MARK launch ... path='...'` names the row |
| Footer | `--- Canoe BDS session complete ---` |

- Rotation orders slots by the header `seq=`, not by FAT mtime: a free slot (lowest index) is used first, otherwise the lowest-sequence slot is evicted, and an unreadable or header-less slot sorts as sequence 0 so it is evicted first. There is no RTC here, so timestamps are meaningless; mtime-based eviction once pinned one slot forever. The chosen file is deleted and recreated, so a stale tail is impossible.
- Flush points are decisive: `SfbLogFlush("pre-fastboot")` when the menu hands over to fastboot, `("pre-launch")` in `SfbLaunchEntry` — the common choke point for menu, timeout, silent default, boot-once and browser launches — and `("pre-export")` before a mass-storage export. A pre-menu-only branch would miss menu mode.
- The body is Canoe's capture ring only. The platform-ring writer that once appended a ring section and a `<<canoe-ring-head>>` anchor was removed, so do not expect those markers in a current file.
- A missing completion footer means a cut-short write; treat the file as partial evidence.

Vendor `UefiLog<N>.txt` is a circular backing array, not an append log. Trailing zeros mean it never wrapped; no zeros means it may have wrapped. On a measured full boot the newest lines sat near offset 4 KiB and early-BDS marks near 56 KiB; `[<microseconds>]` prefixes give timing. Its rotation is broken: `BootCycleCount` never advances, so the same filename is reused, and `SetPosition(0)` without truncate leaves stale tails. Files stay exactly 65,536 bytes regardless of used extent.

## Traps / failure signatures

- Never issue a reboot-target command after arming the recovery reason. `fastboot reboot recovery` and `reboot fastboot` write `boot-recovery`/`boot-fastboot` into the same 32-byte BCB command field and silently discard what you armed; `reboot bootloader` writes no command at all but resets with the bootloader reason. None of them carries the session into recovery, so it is stranded without adb.
- Stock ABL rejects `fastboot boot` with `FAILED (remote: 'Fastboot boot command is not available in locked device')`. A responding fastboot does not prove it is Canoe Super Fastboot; repeat the VOL UP entry.
- A session ending in Super Fastboot never reaches a kernel; there is no `fastboot continue`.
- The platform ring is fixed at about 64 KiB and is not cleared each boot, so missing marks are inconclusive and old marks may survive. A measured launch flush left it 64,008/65,536 bytes full (~97.7%) and a complete boot leaves it 100% full. `ConOut`/`Print()` reaches the platform ring through serial on this target, so menu text can appear in the logs.
- A RAM boot inherits the launching BDS's environment — controllers, logfs binding, watchdog state and hooks already initialised — so first-touch measurements need flashed efisp.
- `submodules/uefi/tests/test_hooks.c` pins marker substrings by `strstr`, so renaming or reflowing one silently voids an assertion: `SFB: MARK hooks-armed`, `SFB: MARK hook-stage stage=locate component=spss`, and `component=reserve universal=1 present=0`. Check every edit against that file; see `log-marker-contract-verification`.
- An `AsciiSPrint` conversion/argument mismatch compiles under `-Werror` and prints varargs garbage, so a plausible-looking log field can be a wrong format string rather than wrong data.
- `SFB: MARK logfs-mount found=.. mounted=.. present=.. status=%r` separates an unbound logfs from a write failure; the common failure is logfs unbound, which leaves the ring intact for the next flush point.
- `read \mu\... failed (Not Found)` proves the row, image, options and namespace rule. `Usage: FdLoader.efi ...` means options did not arrive (older BDS or a rejected line). No row or output means the image was absent at `\tools\...` or the config line was rejected. `is too large` means the payload exceeds its declared window.
- `FdLoader: reserve 0x... failed (Not Found)` means the path resolved and the payload was read, but the firmware allocator refused the window. Do not force it: copying before `ExitBootServices` can overwrite live firmware memory. A payload that needs a fixed link base belongs after EBS, in a shim that owns that address — BDS ships no payload loaders.
- Use staged `SurfaceTools.efi` → **Memory Map** to inspect the target descriptor and find a conventional allocatable region.
- Mount persist from recovery for writes; never write persist while Android runs. Never flash raw efisp for this test.
- Device facts on OnePlus 15/SM8850: efisp 3 MiB (`sde88`, holds `BDS.efi` only), logfs 8 MiB (`sde91`), persist 128 MiB (`sda2`). Recovery `adb reboot bootloader` reaches Canoe Super Fastboot with serial `0000000000000000`, `product=canoe`, `variant=SM8 UFS`.

## Verification

A passing transcript contains:

```text
SFB: MARK launch managed=0 requested-mode=1 effective-mode=1 kind=... path='...'
SFB: MARK image-options chars=N
SFB: MARK image-loaded managed=0 mode=passthrough
SFB: MARK image-start managed=0 mode=passthrough
SFB: MARK image-return managed=0 mode=passthrough status=Not Found
```

Count the option string yourself and require equality with `chars=N`; that is byte-exact delivery proof. `managed=0` and `mode=passthrough` are correct for `tools/` rows because efisp recursion guards and Mode 1/2 hooks must remain unarmed.

A complete Canoe log starts `Canoe BDS session; seq=...; tag=<reason>; captured-bytes=N` and ends `--- Canoe BDS session complete ---`. Require a current `bds-N.log` whose tag matches the action, whose `captured-bytes` is plausible, and whose expected last mark is present. A vendor file's tail is never current-session evidence.
