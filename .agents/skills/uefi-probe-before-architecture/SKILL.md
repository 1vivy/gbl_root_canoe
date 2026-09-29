---
name: uefi-probe-before-architecture
description: "A vendor ABI, HOB, ring address, buffer content, or protocol member is about to become load-bearing: measure it with a read-only standalone EFI probe before designing around it."
---

# Probe UEFI platform facts before architecture

## Use when

Use when a design rests on vendor source rather than device measurement: a ring address, HOB layout, buffer lifetime, protocol revision/member, or capture mechanism. A Canoe probe killed three of four proposed options in one run: a 32 KiB buffer was reused binary (2.4% printable versus 100% real log), a reachable table lacked replay, and vendor re-init was locked out.

## Procedure

1. Decide whether the assumption changes architecture and is cheap to measure. Do not probe facts already load-bearing in shipping code or when measurement costs more cycles than implementing and reverting the feature.

2. Pre-commit each collector's outcomes and actions before running. Ask first whether the platform already writes the desired data somewhere readable; that is often the highest-yield question.

3. Build a standalone app under `edk2/AndroidToolsPkg/Application/<Name>/`, with its own INF and fresh `FILE_GUID`, and list it in the package DSC. Do not add it to BDS: BDS output pollutes the ring being measured.

4. Wire the app into the `tools` make target, both existence check and copy from `edk2/Build/.../AARCH64/<Name>.efi`.

5. Split concerns under the 250-line ceiling: e.g. `Hob.c`, `Fat.c`, `Write.c`, and one file per collector. Put shared prototypes in one header, never duplicated in `.c` files.

6. Make the probe self-sufficient. ABL has not bound logfs, so connect all handles once before filesystem lookup:

   ```c
   Status = gBS->LocateHandleBuffer (AllHandles, NULL, NULL, &Count, &Handles);
   for (Index = 0; Index < Count; Index++) {
     gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
   }
   ```

   Guard with `STATIC BOOLEAN Started`; failures are expected. Locate the output volume by GPT partition name.

7. State in the report that the sweep emits driver output into live rings. An abandoned buffer nobody writes remains unaffected.

8. Make every collector falsifiable:

   - Print live values even when wrong, plus separate `expected=`/`matches-expected=` fields.
   - Validate every firmware-supplied pointer against the current UEFI memory map before dereferencing.
   - Distinguish not found from found-but-unreadable.
   - Report ratios over used extent, not full capacity, and include a raw printable sample.
   - Report protocol revision and non-NULL members; call none merely to test existence.
   - Share one probe list between UI and file dump.

9. For layouts, accept append-only vendor revisions with a `>=` check only where early offsets are defined stable; always print the actual signature/version/pointer/length.

10. Avoid side effects. Calls such as serial `Drain`/`Flush` may push a ring to a UART nobody is listening to and answer nothing.

11. Launch directly from stock ABL:

   ```bash
   fastboot boot build/<Name>.efi
   ```

   This RAM-boots a wrapped PE and writes no partition. While the app runs there is no adb or fastboot gadget.

12. Select the on-screen dump row and let the app exit. Harvest afterward:

   ```bash
   adb devices -l
   adb shell 'mkdir -p /tmp/lf && mount -o ro -t vfat /dev/block/by-name/logfs /tmp/lf'
   adb shell ls -la /tmp/lf/canoe/
   adb pull /tmp/lf/canoe/<report>.txt /tmp/
   adb shell 'umount /tmp/lf'
   ```

   Mount read-only and require the report to be the newest file. Delete-then-recreate it in firmware so a short report cannot retain an old tail.

13. Interpret against a known-good sample from the same device. Zero count alone is weak; 100% versus 2.4% printable over used extent is decisive. A successfully reached negative is strong; failed reach is not evidence of absence.

14. Delete collectors that closed a platform-family architecture question and record the measurement in the dependent commit. Keep collectors only for facts that vary per target, such as ring location or published revision. The probe generally should not ship.

## Traps / failure signatures

- A BDS submenu probe measures the environment after BDS has polluted it.
- Direct ABL launch sees no bound `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL`; an empty filesystem list without the connect sweep is harness failure.
- A mostly empty region makes whole-capacity ratios dishonest.
- A HOB chain is untrusted input; corrupt length can walk off memory.
- `#include <Pi/PiHob.h>` requires `<Pi/PiBootMode.h>` immediately before it.
- `DEBUG` uses double parentheses: `DEBUG ((LEVEL, fmt, ...))`.
- A large static array inflates PE size 1:1. A measured 64 KiB array grew an image from 516,096 to 581,632 bytes; prefer `AllocatePool` when a fail-soft path already exists.
- After concurrent edits, `function definition is not allowed here` cascades often mean one earlier unmatched brace; use the compiler's brace-match note.
- The host cannot communicate while the probe is running; this is expected.

## Verification

Record probe image size/hash, direct-ABL launch path, self-connect distortion note, actual/expected values, pointer-range checks, used-extent ratios, and newest report identity. A conclusion is valid only when the collector reached the target and emitted an interpretable result. Remove one-off collectors after their answer closes the design branch.