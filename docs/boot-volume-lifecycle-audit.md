# Boot-volume lifecycle and path audit — 7.0.0-b4

The accepted overhaul supersedes the raw FAT maintenance design below; see
`canoe-overhaul.md`. Historical checks are evidence for individual primitives,
not acceptance of the new application/mounted-volume architecture.

This is implementation status, not release acceptance. The FAT container change
is not yet connected end-to-end across the native backend, GUI, and packages.
Do not ship the intermediate mixture of new firmware and legacy host paths.

## Storage ownership

Raw `efisp` contains the whole BDS image. `/persist/efisp.fat` is the fixed
32 MiB boot volume. Legacy `/persist/efisp` content is neither a boot fallback
nor a migration source. User-selected input images are external OS paths;
config, BLS, and installed artifacts name paths within the boot volume.

The firmware container owner validates ext4 allocation and FAT geometry before
publishing Block I/O and connecting the FAT driver. It disconnects FAT and
withdraws the public protocol before lending a private Block I/O pointer to USB.
The USB lease retains that pointer until gadget stop, LUN release, and storage
flush/cache teardown succeed. A failed stage retains ownership and a subsequent
attempt retries only unfinished stages. Fastboot USB initialization and reconnect
are blocked while that lease remains outstanding.

Host tests cover partial gadget assignment, failed stop/unassignment/flush,
failed parent cache teardown after freeing the virtual disk, retained ownership,
and retry. The canonical AArch64 BDS build passes. This does not yet establish
behavior of the packaged gadget and FAT drivers on real hardware or guests.

## Logical paths

Rust `boot_path` now owns logical component validation for config images, BLS
references, staged artifact destinations, BLS filenames, and FAT tree conversion.
Firmware config and BLS readers use `SuperFbBootPath.h`. Both implementations
consume `submodules/uefi/tests/fixtures/boot-paths.tsv`.

The shared contract rejects parent/current-directory components, doubled or
trailing separators, drive letters, alternate data streams, Windows reserved
names, and names whose trailing spaces or dots change Windows interpretation.
One leading volume-root separator is accepted. Source image selection does not
use this contract: native file-picker paths remain ordinary OS paths.

Lexical validation is not filesystem confinement. `LocalDir` and artifact
staging still contain direct root joins; their descendant symlink/reparse-point
handling needs review before exposing them as a mounted boot-volume backend.
Do not substitute a one-time canonicalization check for owned directory access
and transaction lifetime. Existing ext4 confinement checks are separate and
must not be weakened when sharing operations with FAT.

## Remaining integration boundaries

- Native backend dispatch includes explicit offline-image and owned raw-export FAT adapters.
  It runs the canonical operations and retains before/after images in an
  external recovery directory. The platform-independent transaction marks FAT
  dirty before changing structures, verifies readback before marking it clean,
  and requires an explicit resume/revert after failure. CLI/GUI source selection and the Android mount adapter remain unconnected. Final-name activation
  is now prepared and validated on private persist images, but its live-source
  adapter and protocol orchestration still need integration.
  Production Android paths still use the legacy root; do not change GUI defaults
  until the owned mount interface is ready.
- GUI `src/lib/artifacts/paths.ts` contains `DEVICE_BOOT_ROOT` pointing at the
  legacy directory. The backend must return the owned mount root; the GUI must
  not independently discover or mount it.
- Consume `canoe-boot-volume=fat16-container-v1` before requesting the BDS
  `boot-root` export. Track ownership across UAC helpers and USB transitions.
- Persist preparation accepts a fully prepared FAT image, stages and renames it
  inside a private persist copy, and records before/after images before any live
  commit. Its review returns the FAT identity and initialized allocation evidence.
  The full deployment receipt still needs to bind this activation record to
  earlier ABL/BDS provisioning and later image writes.
- First allocation/final-name activation, exact readback, durable receipts,
  retries, and cleanup must share a lifecycle. Recovery information must live
  outside temporary extraction directories and the boot root being removed.
- Android needs an owned private loop/vfat mount and checked release before
  removal or export. No competing raw ext4 writer while Android owns persist.
- Review menu/entry/config/driver-list handles before volume handoff and child
  return. No retained root/file pointer may survive controller disconnect.
- Uninstall must release the owned FAT mount before deleting the container;
  selected ABL restoration and verification precede shared boot-root cleanup.
- Implement explicit BDS Save as default without making ordinary selections
  persistent. Keep its config recovery policy compatible with the Rust writer.
- Validate actual ext4 checksum/extent mapping and FAT-driver lifecycle against
  real generated filesystem fixtures, then packaged Linux, Windows UAC/USB,
  KSU, and KSU Next flows, including interruption and flush failures.

Physical phone writes, slot changes, reboots, and formatting are not part of
these source/build checks.

## Backend validation and Windows finding

The offline FAT backend passes the native Rust aggregate and six targeted
checks on Linux and the real Windows 11 guest. Checks include interrupted
partial writes, restart recovery in both directions, failed flush/readback,
foreign target/content/snapshot rejection, byte-identical no-ops, unchanged
unrelated files, Unicode paths, and Windows exclusive file ownership. The
Linux run additionally verifies the resulting FAT with `fsck.fat -n`.

A prepared canonical config has also been staged into a real ext4 image with
exact readback and an independent `e2fsck -fn` pass. This is image-file and
filesystem-adapter evidence, not physical USB or Android-mount acceptance.

The Windows guest exposed an existing error in `LocalDir::write_config`: it
opened a directory using normal file access and tried to flush it. File
publication now has a shared OS adapter: Unix retains rename and directory
fsync; Windows uses a same-volume, write-through `MoveFileExW` after flushing
the temporary file. Recovery records use the same publication operation.
Sources: [MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)
and [FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers).

The audit also found other direct directory/file flush calls in bootstrap and
boot-root cleanup. Review their platform and access-mode assumptions when
connecting uninstall and Android activation; a read-only Windows file handle
cannot satisfy the documented `FlushFileBuffers` write-access requirement.

## Persist activation transaction

`prepare_activation` reads a bounded persist image (up to 512 MiB), performs
allocation and final-name publication on an owned copy using unchanged
libext2fs, verifies `/efisp.fat`, and publishes a durable recovery record. Existing
`efisp.fat` is an installed-volume maintenance case and is not overwritten.
Legacy `/efisp` content remains untouched and is not imported. The low-level Rust
staging entry points now explicitly require an ordinary offline image file.

The shared transaction writes changed 64 KiB ranges, with the primary ext4
superblock marked unavailable first. While that guard is set, its checksum may
not match; this deliberately prevents filesystem use until the complete reviewed
image is flushed, read back, and its clean superblock restored. Recovery validates
the original/desired images, filesystem UUID, source identity, capacity, and
all current bytes before allowing resume or revert. First apply requires the
exact reviewed original; partial-state acceptance belongs only to recovery.

This avoids relying on libext2fs rename as a crash transaction. The upstream
`misc/e2undo.8.in` explicitly says its undo log cannot recover a power/system
crash. Filesystem interpretation stays with libext2fs; Canoe owns reviewed block
commit and recovery. Physical power-loss and USB removal still need fixture
validation; in-process failure injection is not evidence of hardware durability.

Linux image tests prove preparation makes no source changes, existing-volume
refusal is read-only, unrelated calibration and legacy files survive, unchanged
ranges are skipped, interrupted apply can resume/revert, and both resulting
filesystems pass `e2fsck -fn`. A Windows guest test performs actual preparation
through the Windows helper and commit through native file I/O. Its exported
64 MiB result passed independent `e2fsck -fn` on Linux; the only notice was the
VM's filesystem timestamp being less than a day ahead.

The Windows harness is `canoe-boot-manager/e2e/hosts/windows/provision/test-persist-activation.ps1`.
It only accepts ordinary image fixtures, preserves their source hash, and retains
the output for independent inspection. It does not exercise the live raw-device
adapter or touch the passed-through phone.

## Owned raw host transport

`RawVolume` now retains one recognized Canoe export handle and its connection,
OS device identity and capacity. It rechecks acquisition instead of treating a
reusable `/dev/sdX` or `PhysicalDriveN` name as identity. `SectorIo` bounds seeks
and writes, aligns memory/offsets/lengths to the physical sector and performs
read/modify/write for partial sectors. Both readback and commit use this handle.

Windows opens the SetupAPI disk interface with unbuffered, write-through I/O,
validates logical/physical geometry and device identity, then takes the export
offline with a nonpersistent disk attribute. It verifies that state and leaves
it offline after close or failure until explicit export removal; onlining a
partially committed FAT volume would give Windows filesystem access too soon.
Canoe's gadget presents a fixed disk, which is required for this ownership path.
Linux opens with `O_EXCL | O_NOFOLLOW | O_DIRECT` and validates retained block
metadata and USB ancestry. Mounted block volumes cannot be acquired. The
interprocess Canoe device lease remains held until the raw handle closes.
These mechanisms exclude filesystem mounting and competing Canoe operations;
they are not a guarantee against another privileged raw writer.

The test-seams build has a separate owned virtual-disk entry point. The Windows
harness binds its exact 64 MiB / 4K VHDX, rejects boot/system disks, and the native
adapter independently requires the file-backed virtual bus and expected size.
The Linux harness creates its own 64 MiB / 4K loop device; the native adapter
requires that geometry and its fixture backing name. Neither path permits the
passed-through USB phone. Both OS runs verified preparation makes no raw writes,
interrupted commit survives handle close/reopen, explicit resume/revert matches
whole reviewed images, and unrelated calibration and legacy directory data
survive. Both exported results passed independent `e2fsck -fn`; Windows remained
offline until VHDX detachment, and Linux rejected a mounted fixture with EBUSY.
The generic alignment check also covers 512-byte, 4K and 64K physical sectors,
small/cross-buffer writes, bounds and read/write/flush failures.

These are actual OS block-device checks, not yet packaged GUI/UAC/USB acceptance.
The raw transport is connected to FAT backend dispatch; protocol orchestration
and source selection remain to be integrated. Harnesses: `test-raw-activation.ps1` and `test-raw-activation.sh` in
the GUI repository's OS provisioning directories.
Sources: [Windows file buffering](https://learn.microsoft.com/en-us/windows/win32/fileio/file-buffering),
[SET_DISK_ATTRIBUTES](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ns-winioctl-set_disk_attributes),
and [Linux block-device exclusive access](https://docs.kernel.org/5.16/core-api/kernel-api.html).

## FAT operations over the raw export

The FAT backend is now `FatVolume`, with explicit image and reviewed raw-export
constructors. Both run the same canonical config/BLS/artifact operations on a
private tree and the same before/after transaction. A raw constructor requires
an exact 32 MiB expected identity; acquisition delegates to `RawVolume`. The
legacy ext4 caller's device guard is not acquired around this backend, avoiding
a nested lease wait. The retained raw handle remains alive through preparation,
source-drift validation, commit and readback. Failure leaves the USB export owned
by its transport until explicit teardown; no filesystem fallback is attempted.

Actual 32 MiB / 4K raw fixtures passed on Linux and Windows: canonical config
write/read, artifact population, preservation of unrelated files, byte-identical
no-ops and byte-identical failed preparation. Both returned FAT files passed
`fsck.fat -n`. The fixture entry point is test-seams-only and is distinct from
production Canoe USB discovery. Its permitted owned geometries are now 32 MiB
for FAT and 64 MiB for persist activation; physical USB disks remain excluded.
The Windows harness supports `-Scenario Fat`; its new fixture creator only
publishes a newly created empty VHDX, never modifies an existing disk image.

CLI/JSON source selection, capability negotiation and deployment orchestration
still need to select this backend explicitly. None of these checks establish
that the current packaged app has switched away from the legacy persist path.

## Windows automount finding

The user confirmed that the normal FAT USB export must remain mountable for
use outside the GUI. Windows cannot mount a FAT image merely stored inside
ext4 persist; the concern is the explicitly exported FAT LUN. A proposed host
extent-mapping route through persist was rejected as unnecessary complexity;
no such mapped host writer has been implemented.

For managed GUI operations, acquire the specific export's Windows volume,
exclude open filesystem users, flush/dismount it, and retain offline disk
ownership through writes and retry/recovery. A direct FAT LUN can be mounted
before the privileged helper acquires it. Existing files or OS metadata created
before preflight must be preserved, and reconnect/source drift must trigger
fresh validation instead of assuming the original bytes survived. An owned,
mountable 512-sector FAT fixture is still needed: the existing 4K raw fixtures
prove block operations, not Windows automount behavior or busy-volume handling.
Keep normal mounting available outside this managed operation lifecycle.

The same audit found the vendor USB driver acknowledged SCSI SYNCHRONIZE CACHE
without calling backing Block I/O. Producer commit `82b1a78` fixes that dispatch
with checked source staging and a shared `CanoeMsdFlushLun`. Host tests cover
missing/disabled media, absent flush support, failed flush and successful retry
while retaining the LUN association. The rebuilt AArch64 PE passed its identity
check and 142 relocated data slots (84 distinct targets) were verified against
the ELF relocation addends. All three integrated blob copies match SHA-256
`528d393ff505c5dc66e9bfbdaa31fdd5206ee40d8df8c24d512e98cb027095c9`.
UEFI host tests and a fresh canonical BDS build pass.

The native raw transport also explicitly sends SYNCHRONIZE CACHE(10) on the
retained USB handle after the OS flush; it does not rely on host caching policy
to generate that command. Virtual-disk test seams use their native disk flush,
so they do not prove this USB command traverses the physical gadget. The
packaged USB/UAC harness must still exercise that boundary and flush failures.

`make version-check` currently rejects old generated package archives carrying
an earlier BDS, while import digests pass. Rebuild packages after Windows volume ownership
and Android integration are complete; do not label the mixed output
release-ready. The open Windows toolkit at `manual-aa54464` reports 7.0.0-b3;
it has not yet been replaced by a completed b4 package.
