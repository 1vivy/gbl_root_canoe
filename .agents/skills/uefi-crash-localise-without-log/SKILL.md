---
name: uefi-crash-localise-without-log
description: "Pre-OS UEFI crash or hang (BDS, DXE, or app) with vanishing DEBUG marks, ambiguous screen, or stale logfs: self-clearing breadcrumbs, then an owned status-code ring for durable capture."
---

# Localise a UEFI crash when the log dies

## Use when

Use after a BDS, DXE, or UEFI app hard-faults or hangs before an OS, leaving `/proc/bootloader_log` or logfs stale. Qualcomm commonly flushes the platform ring only on ExitBootServices, reset notification, capsule reset, or provisioning. Start with self-clearing screen breadcrumbs; build a payload-owned status-code ring when evidence must survive routinely. What the firmware itself writes is `\canoe\bds-N.log` in rotated slots, not the vendor `UefiLog*.txt`.

## Procedure

1. Rule out cheap offline causes before instrumenting:

   - Compare bundled and fixed blobs with `sha256sum`.
   - Use `git merge-base --is-ancestor <other> <yours>` rather than commit dates to establish ancestry.
   - Confirm clang passed `--apply-dynamic-relocs`; count distinct pointer-like `.data` qwords via `llvm-objdump -s -j .data`. Healthy BDS images measured about 120–126; standalone apps about 40; one distinct value is broken.
   - `llvm-objcopy --dump-section` can fail on these PEs; parse `llvm-objdump -s`.

2. Attribute the observed moment correctly. A screen frozen on the menu can mean the timeout auto-launched the default. If interaction avoids the crash but waiting triggers it, investigate launch/timeout, not drawing.

3. Add one screen breadcrumb per phase between entry and the first full-screen UI:

   ```c
   VOID SfbBootMark (IN CONST CHAR16 *Stage) { DEBUG ((EFI_D_INFO, "SFB: MARK stage=%s\n", Stage)); }
   ```

   The first `SfbBeginScreen`/`ClearScreen` erases them on success; on fault, the last visible line identifies the phase. Current Canoe phases: `fatstack`, `logfs`, `menu:begin`, `menu:config`, `menu:bootroot`, `menu:discover`, `menu:rows`, `scan:locate`.

4. Put one marker around each newly added call, never per loop iteration. Map stages to owners before the operator boots, including a “menu drew normally” result for timing-sensitive failures.

5. If two actions crash, list the changed calls on each path and intersect them. The shared call localises the defect without a log.

6. Make acquisition transactional. After a start returns success, verify with the artefact that proves the resource materialised — a protocol count, a published LUN, a `taken=1` mark — plus its cleanup. On `taken=0`, finish the claim immediately and return the failure; never leave a partially claimed shared gadget for the next caller. Claim ownership from evidence, not status.

7. Guard every teardown with ownership and scope disconnects to the exact protocol brought up. Never blanket-disconnect every `EFI_PCI_IO_PROTOCOL`; that can tear down storage being read.

8. Re-resolve handles after teardown. `DisconnectController` destroys children, so a pre-resolved `EFI_BLOCK_IO_PROTOCOL` is dangling — the mass-storage export reconnects controllers, so a cached filesystem handle can go stale mid-boot. Pass a name and resolve inside the callee after teardown; re-resolve on every menu redraw.

9. For late removable media, a handle-only menu rebuild is insufficient. Re-run the connect pass, but spend a one-shot “connect is fresh” flag so the first build does not duplicate acquisition (see `fat-stack-start reused=1`).

10. When breadcrumbs are insufficient, the vendor-side question — whether a public flush routine or config knob is reachable at all — belongs to `uefi-vendor-dxe-inventory-and-staging`; read it there rather than guessing from BSP names.

11. Tee all `DEBUG()` messages through `EFI_RSC_HANDLER_PROTOCOL` when the DSC resolves DebugLib through status codes:

    ```c
    gBS->LocateProtocol (&gEfiRscHandlerProtocolGuid, NULL, (VOID **)&Rsc);
    Rsc->Register (Callback, TPL_HIGH_LEVEL);
    ```

12. At `TPL_HIGH_LEVEL`, only decode and copy into one preallocated ring. Use `ReportStatusCodeExtractDebugInfo` and `AsciiBSPrint` for its `BASE_LIST`; use `AsciiVSPrint` only for ordinary variadic forwarding. Do no allocation, I/O, or Boot Service calls. Skip opaque records: arbitrary payload decoding is not bounded at HIGH.

13. Register an ExitBootServices event that sets a `volatile BOOLEAN`; the callback checks it before touching payload code. Do not unregister from the EBS notify. Unregister normally on every return path.

14. Rotate the owned ring in place at snapshot time using three reversals. When overwritten, make the first line state dropped bytes. Avoid a second linearisation buffer in a fixed-size boot image. Keep the borrow flag held until release, so a second snapshot is refused instead of rotating the ring out from under a handed-out pointer.

15. Open the next slot of the `\canoe\bds-N.log` rotation and write the header (flush tag and captured byte count), then the capture, and a `--- Canoe BDS session complete ---` marker last. Flush explicitly, and `File->Delete` on any error so a short report cannot keep an old tail. A completion marker makes partial writes visible.

16. A vendor platform-ring reader/writer is no longer part of this tree. If one is added back, emit a distinctive `DEBUG()` anchor immediately before reading, locate it as the write head, and write `[head..end]` then `[start..head]`. Never mutate the live vendor buffer. Trailing zeros mean it never wrapped.

17. Flush before a one-way fastboot loop (`SfbLogFlush ("pre-fastboot")`), then export the partition as USB mass storage in the same session.

## Traps / failure signatures

- Vendor `UefiLog*.txt` after a crash may belong to the last successful boot. Require a run-specific mark in the same read, and prefer our own session-sequenced `\canoe\bds-N.log` (`seq=` header).
- `SFB: MARK logfs-mount found=1 mounted=0` counts only volumes this call connected; an already-bound filesystem is reported separately as `present=`. `found=1 mounted=0 present=0` with `status=Not Ready` is a genuine miss.
- `Print()` breadcrumbs survive on screen but ordinarily do not replace a durable log.
- `submodules/uefi/tests/test_launch.c` links real `SuperFbEntries.c`; add stubs for new firmware helpers beside the existing ones (`SfbBootMark`, `SfbBeginScreen`).
- `StartController(...)=EFI_SUCCESS` with zero USB2 HC and PCI I/O handles means acquisition failed materially.
- `InterlockedCompareExchange32` needs often-unmapped `SynchronizationLib`; a documented test-then-set guard may be preferable.
- Vendor info-block HOBs may contain a pointer, not an inline struct. Follow the vendor accessor and validate signature/version before dereference.
- A 64 KiB static ring grows the PE 1:1; capacity must be checked.

## Verification

For breadcrumbs, the last visible stage must narrow the fault to one phase, while a healthy boot clears the lines. For durable capture, require an ordered ring section, an honest dropped-byte line when wrapped, the current flush tag and byte count, and a final completion marker. Remove breadcrumb dwell after durable capture proves reliable; keep only cheap self-clearing phase marks.
