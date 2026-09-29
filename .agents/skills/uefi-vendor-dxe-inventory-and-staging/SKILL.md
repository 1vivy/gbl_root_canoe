---
name: uefi-vendor-dxe-inventory-and-staging
description: "Vendor UEFI facility unreachable, absent, or uninitialized: source-level gate/config-store/callability checks, then DXE inventory, depex and private-ABI decode, and authenticated staging."
---

# Vendor UEFI facility reachability and DXE inventory

## Use when

Use before concluding a Qualcomm/OEM DXE or facility is absent or usable, and before staging “one more” driver. Symptoms: zero protocol inventory, universal `Device Error`/`Invalid Parameter`, a documented config knob that appears ignored, a retail gate, an undocumented ABI, or a `LocateProtocol` that fails while the BSP source clearly implements the function. Presence, dispatchability, entry-point dependencies, publication, usable initialization, and permission to use are separate questions.

## 1. Source-level reachability traps

Each of these has inverted a conclusion before. Check the cheap decisive fact first.

| Trap | Decisive check |
| --- | --- |
| “The function is public, so we can call it” | Quote the protocol struct member. UEFI has no dynamic symbol resolution between images. |
| “Set KEY in somefile.cfg” | Trace the value to the code that reads it at runtime, then to the signed build artifact that produces that store. |
| “The retail gate blocks us” | `grep` every call site of the gate and enumerate which operations actually pass through it. |
| “Raise the buffer size” | Find where the allocation lands at each size and which consumer reads that exact address. |

1. **Callability.** A non-`STATIC` function with a public header declaration is not callable from another image. In order: (a) which INF owns it and its `MODULE_TYPE`/`LIBRARY_CLASS` — a `DXE_DRIVER` library is same-image only; (b) whether a **protocol** exposes it, read member by member, never from the protocol's name; (c) whether the only protocol route is a wrapper doing far more than wanted. Canoe case: `WriteLogBufToPartition` was public in `FwUpdateLib.h`, but `PlatformBdsLib.inf` is `DXE_DRIVER`, `EFI_PLATFORMBDS_PROTOCOL` has no flush member, `EFI_ULOG_PROTOCOL` has no flush member, and the one protocol route (`FwProvision`) wrapped the call in security/CRC/validation side effects. Three dead routes that looked live from names alone.
2. **Config knobs go stale.** Follow the chain: documented key → `GetConfigValue`/`GetConfigVal` implementation (does it read a file at all?) → what populates its table (DT parse? HOB? file?) → where that blob comes from at runtime (shared IMEM cookie? FV?) → which build step generates it (`buildconfig.json`) → whether that artifact is signed, and by whom (`Security_profile/*.xml`). Canoe result: the documented `uefiplat.cfg` was a deprecated stub still embedded as freeform; the live store was `platformconfig.dtsi` compiled into the separately signed `xbl_config` partition (`authenticator_oem="TME-FW"` in the Pakala/Eliza/Bonito security profiles). The knob was real and the doc honest — and still unreachable. **State blockers at partition granularity**: “needs an authenticated `xbl_config` update” ≠ “needs XBL rebuilt”.
3. **Gates gate one operation, not all.** Finding `if (RETAIL && IsDebugPartition(x)) return ERROR;` does not mean the operation is blocked. Canoe case: the gate lived in exactly one function — the *mount* path — while the *write* path (`WriteFile` → `OpenFile`) reached the filesystem protocol directly. Retail blocked the vendor's own mount, and the payload's mount was what unblocked the vendor's writer.
4. **Enlarging a buffer relocates it.** Typical shape: a request inside a fixed reserved region gets a static allocation there; a larger one gets a dynamic allocation elsewhere, clamped to a max. A consumer reading the **fixed address** (kernel carveout, crash-dump region, a later stage) then reads someone else's memory. Confirm the consumer from the *device* DT `reserved-memory` nodes, not the BSP's.

## 2. Inventory from the device image

1. Capture a read-only live protocol-GUID census with handle counts once. Enumerate only; do not call unproven vendor methods on stopped cores or ports.
2. Dump with `dd if=` only:

   ```bash
   adb shell 'for p in uefi_a imagefv_a xbl_a abl_a; do d=/dev/block/by-name/$p; [ -e $d ] && echo "$p $(blockdev --getsize64 $d)"; done'
   adb exec-out "dd if=/dev/block/by-name/uefi_a bs=1M 2>/dev/null" > uefi_a.img
   python3 -c "d=open('uefi_a.img','rb').read(); i=d.find(b'_FVH'); print(hex(i-0x28) if i>0 else 'none')"
   python3 -c "d=open('uefi_a.img','rb').read(); open('uefi_a.fv','wb').write(d[0x1000:])"
   uefiextract uefi_a.fv all
   ```

   Locate the FV header rather than assuming `0x1000`; the slice above is an example once confirmed. `not a single Volume Top File` is benign. Raw strings miss compressed FV sections.
3. Read the CSV/report, not hundreds of directories, then extract the `PE32 image section/body.bin` beneath the UI-named module directory (paths use UI names, not GUID text):

   ```bash
   grep -iE 'xhci|usb' uefi_a.fv.guids.csv
   grep -iE 'xhci|usb' uefi_a.fv.report.txt
   ```

4. Interpret the image boundary. Typical, not guaranteed: `abl_a` has one bootloader app; `xbl_a` has boot-time DXEs in an ELF; `uefi_a` has a fuller set often absent from Android boot; `imagefv_a` is auxiliary. A driver in a non-executed image is present on device but unavailable to the current payload.
5. Record source partition, exact dump command, parent image SHA-256, module GUID, size, and module SHA-256 under an ignored artifact path. Prefer a device-extracted PE over third-party chipset dumps.

## 3. Classify runtime state

| Symptom | State/action |
| --- | --- |
| `LocateProtocol` fails | absent from the live environment; inventory its producer |
| Calls fail for every index | wrong index space or empty backend |
| Count 0 / MAX/NONE sentinel | published but uninitialized; stage the dependency stack |

Call index-free inventory getters first. A vendor “active index” may be an SDAM-slot enum while sibling calls expect a PMIC device index; read each `@param` and probe named, then documented 0/1 candidates.

## 4. Dependency, ABI, and staging

1. Decode the module depex: `0 BEFORE`, `1 AFTER`, `2 PUSH <GUID>`, `3 AND`, `4 OR`, `5 NOT`, `6 TRUE`, `7 FALSE`, `8 END`, `9 SOR`; decode EFI GUID with `uuid.UUID(bytes_le=...)`.
2. Recover entry-point dependencies separately from vendor INF `[Protocols]`, resolving symbols through package DEC GUID values. `TRUE` depex says only that the dispatcher may call entry; a missing protocol located inside entry can still return `Device Error`.
3. Cross-reference both dependency sets with the live census. Any missing entry dependency makes startup impossible in that environment/load order — find its producer and inventory all FVs; if no producer exists, stop before hardware risk.
4. Decode a static protocol instance first: `llvm-objdump -h` for `.text`/`.data`, parse `llvm-objdump -s` because `llvm-objcopy --dump-section` may fail, then scan aligned `.data` qwords for known revision constants. Validate function pointers against `.text`, NULL slots, and non-pointer fields from the vendor initializer.
5. Generate offsets with a tiny compiled C oracle including the real header and `offsetof`; enums are 4 bytes and break hand-calculated pointer runs. Respect the declared revision boundary even if adjacent qwords look callable. Require both revision coverage and a non-NULL pointer.
6. If the instance is built at runtime, resolve AArch64 `adrp` + `add`/`ldr` references and find `InstallMultipleProtocolInterfaces`: `x1=&GUID`, `x2=interface`. Verify per binary that PE file offsets, RVAs, and printed addresses coincide. Distinguish `add` address-of from `ldr` load-from-page, and bound string-reference analysis to a real prologue → `ret`.
7. Useful 64-bit `EFI_BOOT_SERVICES` offsets: `0x50 CreateEvent`, `0x58 SetTimer`, `0x68 SignalEvent`, `0x70 CloseEvent`, `0x80 InstallProtocolInterface`, `0x98 HandleProtocol`, `0xc8 LoadImage`, `0xd0 StartImage`, `0xe8 ExitBootServices`, `0xf8 Stall`, `0x100 SetWatchdogTimer`, `0x118 OpenProtocol`, `0x138 LocateHandleBuffer`, `0x140 LocateProtocol`, `0x148 InstallMultipleProtocolInterfaces`, `0x150 UninstallMultiple`. Runtime `ResetSystem` is `+0x68`.
8. Stage only after offline proof. Load in vendor `APRIORI.inc`/`DXE.inc` order; hardware-table builders must run after table producers. Use `LoadImage`/`StartImage` from the boot root. If a device-extracted unsigned image returns `Access Denied`, stop: require explicit owner authorization and an approved authenticated staging path; never weaken or bypass firmware authentication.
9. Print one status row per driver. Load heavy stacks only on the explicit operator row that needs them. Gate power actions behind explicit operator choice and a hardware interlock; make restore idempotent on every exit.
10. For a decoded private protocol, gate calls on locate success, exact revision, and non-NULL member. Set the one-shot flag before invoking. Emit one greppable mark per absent/revision/member/call outcome.

## Decomposing these trees

BSP trees are ~800 MB. Fan out read-only scouts on orthogonal questions and forbid overlap:

| lane | question |
| --- | --- |
| allocation | what is the buffer/resource, who allocates it, at what size, which image owns it, what is the size knob |
| trigger + callability | what invokes the facility, is it protocol-exposed, what are the hazards of invoking it early |
| consumer | who reads the result later, from what address/region, with what size assumption |
| our side | what does *our* tree already link and have available — libraries, GUIDs, existing precedents |

Demand `path:line` on every load-bearing claim, and require an explicit “could not determine” rather than a plausible inference. Scouts message findings as they land so siblings do not re-derive. The “our side” lane is the one people skip and the one that most often supplies the answer.

## Before concluding

- Prefer a mechanism needing **no** vendor cooperation over one that hooks vendor internals, when both are affordable. Owning a parallel buffer beats reading someone else's.
- Check whether your own tree already does the thing — a shipped tool that writes the same partition settles “is it possible with our privilege” outright.
- Separate “what this node is” from “whether this process may use it”; conflating identity with permission yields both false blocks and false green lights.
- If an existing call in your tree is the enabler for a vendor path, say so — it changes whether that call may be moved or removed.

## Traps / failure signatures

- Depex constrains dispatch, not presence. A source claimed to have standard-only depex plus an absent runtime protocol still needs FV extraction; only extraction proves presence.
- Census presence proves publication, not an initialized function. `Success` with zero count or a MAX/NONE sentinel is a negative result.
- An errored interlock query reported merely as “unsafe” hides call bugs; log status separately.
- Load order can permanently cache empty hardware tables even when the depex is satisfied.
- Verify staged bytes after unmount/remount; a copy may land on a ramdisk shadow.
- A `.data` pointer census with one distinct value indicates missing `--apply-dynamic-relocs`; healthy vendor drivers show tens.
- A few dozen byte differences in a 40–90 KiB independent copy may be build stamps; thousands indicate another build.
- Missing log lines are not absence unless the session flushed and enough time elapsed.

## Verification

Require: device-image inventory rather than raw strings; module PE/depex and provenance hashes; depex plus INF dependencies all present in the live census; static instance validated by revision, offsets, initializer scalars, and NULLs, or a runtime vtable recovered from the install call; relocation census healthy; vendor-order staging transcript complete; target protocol inventory nonzero; unmount/remount byte verification. Disassemble your compiled caller and confirm the expected boot-service/member offsets, GUID bytes, and outcome strings in the final image.
