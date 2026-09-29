---
name: canoe-bds-deploy-verify-ladder
description: "Deploy and prove a rebuilt Canoe BDS when efisp is load-bearing, write-path regressions are possible, RAM-boot state may mask prerequisites, or reported status facts are stale."
---

# Deploy and prove a rebuilt Canoe BDS

Use before flashing any `BDS.efi`, after a fastboot write-path fix, when RAM-boot succeeds but flashed efisp fails, or when an install receipt disagrees with a getvar/UI. The ladder separates image identity, inherited boot-path state, safe destructive proof, real reboot, and per-boot stale facts.

Two artifacts, never conflated: raw `efisp` holds the whole `BDS.efi` image, and `persist/efisp.fat` is the FAT16 boot-root container this ladder does not prove.

## Procedure

1. Enforce one device claimant. Stop GUIs, Canoe CLIs, adb, fastboot pollers, qdl, and any process that opens the USB endpoint. Exec-spawn and kernel-uevent monitors are safe because they do not claim it.

2. Build through `canoe-bds-rebuild`; record candidate size and SHA-256. Capture before-values from the broken/flashed loader:

   ```bash
   for v in canoe-bds current-slot max-download-size max-fetch-size \
            partition-size:efisp partition-size:logfs; do
     printf '%-22s ' "$v"; fastboot getvar "$v" 2>&1 | head -1
   done
   ```

   A measured regression pair on this target: download/fetch `1610612736` (`0x60000000`) broken vs `805306368` (`0x30000000`) fixed, and `partition-size:efisp` missing vs real. `canoe-bds` alone can match across stale/current loaders, so pair it with a value the broken image cannot produce.

3. Before blaming a write, identify when every reported fact was measured. A `STATIC`/global assigned once at startup is frozen for the session. Rank evidence: direct medium read now > operation receipt (generation/digests/counts) > frozen getvar. Canoe's `canoe-boot-root` is published from the observation the boot already made and cannot reflect a same-session install.

4. Prove a disputed install host-only with the shipped binary and a real filesystem fixture; read it through both the product verb and a raw helper. On-device, get one direct directory listing. Do not re-probe a deliberately frozen fact or turn unreadable into success.

5. RAM-boot the candidate, then repeat the discriminators:

   ```bash
   BDS=<path>/submodules/uefi/build/BDS.efi
   fastboot boot "$BDS"
   until fastboot devices | grep -q fastboot; do sleep 1; done
   ```

   Do not press Volume Up during normal bring-up: it diverts to the menu. A silent default can still auto-launch, so intercept that window when configured.

6. Stop if the discriminator remains old. The RAM boot did not take, and asking the broken loader to replace itself is unsafe.

7. If RAM-boot behavior differs from flashed efisp, collect both logs and compare environment marks, not feature prose:

   ```bash
   strings blog.bin | awk '/SFB: MARK|connected .* handles|file systems are/{print NR": "$0}'
   ```

   Measured example: `handles=367 usbfn=1` after fastboot entry vs `handles=363 usbfn=0` from efisp, while connect-all counts matched. `fastboot boot` inherits USB initialization, open partitions, disabled timers, and installed protocols from its launching fastboot.

8. Locate the failing routine's first `LocateProtocol`; compare it with the failing census. `Not Found` from `%r` can mean missing protocol, not partition. Absence of `fb-usb-start` and presence of `SFB: power-on key=… decision=… window=…` identify boot paths.

9. Give a missing prerequisite one guarded home. If already present, return silently; otherwise run vendor bring-up, re-check presence outside `DEBUG`, and emit one mark only on the missing path. A deployment-path test must execute this branch; RAM boot may turn it into a no-op.

10. Prove the write fix on inactive `abl_b` while current slot is `a`. First fetch a baseline, then issue consecutive writes with nothing between:

   ```bash
   IMG=<abl image>   # 278528 bytes on this target
   fastboot fetch abl_b /tmp/abl_b.before
   fastboot flash abl_b "$IMG"
   fastboot flash abl_b "$IMG"
   fastboot fetch abl_b /tmp/abl_b.after
   cmp -n 278528 /tmp/abl_b.after "$IMG"
   tail -c +278529 /tmp/abl_b.after | tr -d '\0' | wc -c
   ```

11. Do not use identical-content proof alone. Run an A/A pair and B/B pair, fetch after each, count changed bytes in both directions, and finish on desired content. Check image range, trailing partition bytes, and header magic.

12. Start a fresh session for efisp so it is the first flash:

   ```bash
   fastboot reboot bootloader
   until fastboot devices | grep -q fastboot; do sleep 1; done
   fastboot boot "$BDS"
   until fastboot devices | grep -q fastboot; do sleep 1; done
   fastboot getvar partition-size:efisp
   fastboot flash efisp "$BDS"
   fastboot fetch efisp /tmp/efisp.after
   cmp -n <size> /tmp/efisp.after "$BDS"
   ```

   `efisp` is a raw whole-partition image. The reported partition size must exceed the BDS size.

13. Reboot and prove USB teardown/re-enumeration occurred before trusting post-reboot getvars. A real SM8850 cold boot is about 11 seconds; RAM handoff 1–2 seconds. Then re-check fixed discriminators from the flashed image.

## Traps / failure signatures

- `ExchangeFlashAndUsbDataBuf` swaps pointers only on flash; RAM boot is download, making the next flash the first and safe write in that session.
- A `getvar` or fetch between consecutive test flashes invokes `WaitForFlashFinished` and masks the defect.
- `fastboot fetch` is Canoe-local (`fetch:partition[:offset[:size]]`); stock fastboot cannot provide byte-exact read-back.
- A failed write is not guaranteed to abort before flash. Verify rather than assume the inactive slot is intact.
- `SendBuffer()` failure, device disappearance, then USB `05c6:900e` is the classic second-write signature. Do not re-arm ramdump: a measured dump was TZ-wrapped, ~37% printable, and unusable.
- A truncated partition table cannot name efisp and silently forces updates through stock fastbootd. Fix the ceiling first.
- From Canoe, `reboot bootloader` returns through ABL → efisp → Canoe. `reboot recovery` and `reboot fastboot` restart through the ordinary boot path after writing their own BCB command; they are not lower-level fastboot sessions.
- A green RAM-boot feature test is unfalsifiable when the fastboot harness supplies its prerequisite.
- Keep toolchain constant while diagnosing. Local and `gbl_builder:latest` hashes/sizes differ due to clang and `__DATE__`/`__TIME__`.
- Incremental ~2-second builds can be valid; require the source clang line and new marker bytes in `build/BDS.efi`.
- Measurement-code trap: `b'\\x00'` is four printable bytes, not a NUL.

## Verification

Advance only when each gate passes: candidate identity differs from before; RAM discriminators prove the candidate is live; flashed-only prerequisite marks show successful guarded bring-up; A/A and B/B inactive-slot writes fetch byte-exact with expected remainder; efisp is the first write of a fresh session and fetches byte-exact; a real re-enumeration occurs; final fixed getvars come from flashed efisp. Classifier regressions must pin direct-read/receipt precedence over frozen per-boot facts.
