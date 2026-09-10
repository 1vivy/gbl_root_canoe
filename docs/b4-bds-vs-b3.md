# BDS 7.0.0-b4 versus 7.0.0-b3

Compared the local b3 branch `865a8cd91ab18040b6e8d09f6e0b76776b0424e4`
with b4 implementation `b8d00b090e322bd28fc37071e5a0f92b05be94e5`.
The current BDS binary is 585,728 bytes, SHA-256
`15333bfca3508a5d2a268f092b7aeab0017e414f55a18434f4ef221b9f23312e`.
This is a source comparison against that branch, not an assertion that every
previous binary labelled b3 was built from exactly that commit.

The UEFI diff spans 48 files (+2,661 / −278 lines), including tests and one
replaced USB driver binary. Production EDK II source/build files account for
35 files (+1,699 / −269). The main change is storage and USB lifecycle; it is
substantial even though the Android mode projection remains unchanged.

| Area | b3 | b4 | Physical check that matters |
| --- | --- | --- | --- |
| Boot root | Files under ext4 `persist/efisp/` | Fixed 32 MiB FAT16 file `persist/efisp.fat`, mounted as its own UEFI filesystem | Discover the new container; ignore legacy entries; read loader and sidecars correctly |
| Ext4 access | Read files/directories through Ext4Dxe | Ext4Dxe also produces a bounded, validated map of the initialized container's extents | Actual persist feature bits, clean state, extent/checksum/allocation compatibility |
| FAT block I/O | Existing physical FAT volumes | New logical disk translates FAT reads/writes into only the file's allocated ext4 data blocks | FAT save/readback; unrelated persist contents and metadata remain intact |
| USB exports | Raw persist/logfs export | Separate removable `boot-root` export; raw persist/logfs remain available | Windows assigns a usable native FAT volume; ordinary external mounting works |
| USB ownership | Previous export cleanup | Explicit retained ownership through gadget stop, flush and detach; firmware FAT is disconnected before USB owns it | Repeated export → safe eject → Super Fastboot cycles; no stale handles or caches |
| Device identity | Fastboot used an all-zero serial | Fastboot and mass storage use the platform USB serial; container-bound export also checks ext4 UUID/inode/generation at final handoff | Actual platform serial query succeeds and both USB personalities match |
| Default persistence | Menu selection/config reading | Explicit “Save a default entry and mode”, staged publication and validated `canoe.cfg.prev` fallback | Save survives reboot; an ordinary one-shot selection does not alter it |
| Paths/config | Earlier separate validation | Common path restrictions across firmware/tools; tighter BLS/default parsing, duplicate handling and read/close error propagation | Existing intended EFI/BLS paths work; malformed inputs cannot become launch paths |

## What did not change in BDS

- The raw efisp partition still holds the whole BDS image. There is no new GPT
  layout or partition-resize step.
- `Hook/`, `SuperFbLaunchPolicy.c`, the logfs implementation, and standalone EFI
  tool sources are unchanged relative to the selected b3 baseline.
- The Mode 0/1/2 managed-ABL projections, recursion guard and GM2P/TZ-map formats
  are unchanged. Their integration still needs a real child-launch check through
  the new boot root.
- The ABL patching algorithm is unchanged. Its only patcher-directory diff is
  deterministic Windows linking. GUI image preparation and data assessment are
  separate application changes, not new BDS projection rules.
- Legacy directories are not migrated or erased. Managed entries must be
  recreated in the new container.

## New failure boundaries to validate first

`Ext4MapImage` refuses unsupported filesystem features, dirty/orphaned state,
uninitialized or missing extents, overlapping/metadata extents, invalid allocation
or checksums, and a changed inode/media. `CheckFat` requires the exact container
geometry, clean FAT flags and matching FAT copies. Refusal should leave the
container unavailable rather than treating the old ext4 directory as a fallback.
These checks mean a general FAT image or a sparse copy is not automatically a
valid provisioned boot root. Prefer the canonical provisioner for allocation.

The platform USB serial is now required instead of fabricating a shared identity.
Failure to obtain it can prevent Fastboot USB startup. This is a specific new
hardware dependency to check before assuming b3's connection behavior carries
over. New capability variables include `canoe-boot-volume=fat16-container-v1`
and `canoe-device-identity=platform-usb-serial-v1`.

Reaching Super Fastboot is necessary but does not establish that flash/readback,
persist export, FAT export or ejection works. Validate those separately before
depending on this BDS for recovery. An existing known boot path and suitable
firmware restore images remain relevant until the new path is confirmed.

## Proposed first hardware sequence

1. After RegionalHybrid, capture fresh build/slot/lock state and partition hashes.
   Prior cached boot/vbmeta images are reference material, not new graft donors.
2. Once the RAM-boot test is authorized, use `fastboot boot BDS-7.0.0-b4.efi`
   from the existing Super Fastboot session. The raw UEFI file is supported;
   the tooling handles packaging. Confirm the running BDS version afterwards,
   since command acknowledgement precedes child loading.
3. Keep this first test to BDS startup/menu and Super Fastboot basics: device
   detection, real serial, capability/slot variable reads and reconnect behavior.
   Flashing, container provisioning and managed Android boot are later stages,
   not prerequisites for this initial check.
4. Separately provision/inspect an empty canonical container, after approving
   that persist write. Check export, native FAT mounting, a benign file roundtrip,
   ejection and independent readback. Keep bootloader/image changes out of this
   individual test so its result is attributable.
5. Populate a reviewed loader triplet and entry, validate explicit default saving,
   then test managed boot and actual data access under the intended mode.

On 2026-09-08, the user supplied the phone running b4 for Linux read-only checks.
Version/slot/identity probes and three full raw persist export/read/eject cycles
passed; the final persist hash matched the initial fastboot capture. Missing
`efisp.fat` was confirmed and boot-root export refused without losing Fastboot.
No agent boot, flash, provisioning or format command was issued. Detailed results
and remaining observations are in the sibling app's
`docs/b4-physical-readonly-2026-09-08.md`. Compile/contract/guest proof is recorded
in `docs/canoe-overhaul.md` and the app's `e2e/OVERHAUL-ACCEPTANCE.md`.

Historical terminology note: the “recursion guard” above means the prepared ABL patch disables its `efisp` lookup (`efisp` → `nulls`). Current BDS has no runtime efisp Block I/O hiding hook. This document describes the earlier comparison, not a runtime Escape implementation.
