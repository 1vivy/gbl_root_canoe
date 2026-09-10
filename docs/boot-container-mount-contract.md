# Boot-container mounting responsibilities

Opening `persist/efisp.fat` should be a filesystem operation. Its caller asks for
a mounted boot root and receives either the root or the underlying failure.
It should not have to repeat a filesystem-admission checklist.

The reference pattern separates three responsibilities:

1. Open the backing file using the outer filesystem driver.
2. Expose that file's bytes as a bounded disk, with correct read/write, flush and
   ownership semantics.
3. Connect the ordinary filesystem driver to that disk and use its file API.

EDK2's [RAM-disk registration][ramdisk] initializes and publishes Block I/O, then
calls `ConnectController`; it does not parse the filesystem inside the disk.
Linux's [loop driver][loop] delegates backing-file I/O to file operations and
flushes through `vfs_fsync`. These establish the abstraction boundary, not a
drop-in implementation for CANOE-BDS. GRUB's [loopback implementation][grub]
likewise delegates reads to its file API, but that reference does not implement
writes.

## Current CANOE-BDS implementation

`SfbContainerMount` already presents one high-level entry point. The integration
has an unusual lower-level constraint: the inherited Ext4Dxe supports reading
files, not writing them. `Ext4MapImage` consequently maps the initialized file's
allocated extents, and `SuperFbImageDisk` writes within those extents without
allocating blocks or changing ext4 metadata.

The physical mapping must remain bounded to the file, account for holes and
unwritten extents, preserve block alignment, propagate I/O failures and expire
before another owner changes persist. Successful FAT mounting alone cannot
establish those properties. Removing them without replacing the backing-file
write implementation would widen what a malformed extent can overwrite.

The former `CheckFat` in `SuperFbContainer.c` parsed the BPB, insisted on the
provisioner's precise FAT16 geometry and compared both FAT copies before
connecting FatDxe. That duplicate filesystem policy has been removed. FatDxe
determines whether the exposed disk is mountable; provisioning size policy stays
at creation time and physical-range safety stays in the image disk. Do not add repeated whole-image reads,
hash gates or read-only remount cycles to ordinary mounted-file operations.

Filesystem close/flush and USB handoff are distinct operations. Finish and
synchronize writes before giving another owner the disk; disconnect cached
filesystem views before their backing storage can change. A flush failure must
remain visible, and a rejected export must not be reported as an active USB
storage session.

## Lifecycle ownership

`Ext4OpenImageFileSystem` and `Ext4ReleaseImageFileSystem` live in Ext4Dxe with
its driver binding. They reuse this instance's filesystem or explicitly hand
over from the previous RAM-loaded instance's driver. They never interpret a
foreign provider's private file structures. Release targets only this driver's
binding, preserving Disk I/O and the physical controller. Ext4Stop must uninstall
its filesystem protocol before freeing the published state; a refused uninstall
leaves it intact.

`SuperFbContainer` owns the file-backed Block I/O and FAT mount lifetime.
`SuperFbMassStorage` owns the raw-export boundary: before exporting persist it
also releases an ext4 cache installed by the initial controller-discovery pass,
even if no container map was ever opened. A remaining foreign filesystem or a
refused release prevents the export. Fastboot EFI RAM boot releases the current
container before starting the child image.

## Diagnosis and physical qualification, 2026-09-10

The retained post-deployment image contains a valid, empty FAT16 container whose
bytes match the completed provisioning receipt. The CRC16 backport admits its
outer ext4 filesystem. Mount-stage logs isolated `EFI_INVALID_PARAMETER` to
image-disk initialization. The linked `SuperFbImageDisk.obj` still enforced a
48 MiB limit from an older header, while the mapper and current source allowed
256 MiB. Its retained preprocessor output and object instructions independently
confirmed that mismatch. The canonical BDS target now discards its compiled
output tree before rebuilding; a newly linked EFI alone cannot detect stale
constituent objects.

There was a separate driver-contract error: `ConnectController` returns
`EFI_NOT_FOUND` when it starts no new drivers, including an already-mounted
filesystem. The wrapper now checks the published filesystem, following the
[EDK2 connection contract][connect], instead of treating that result as proof
that mounting failed. RAM-loaded instances explicitly select their own ext4
provider, and cleanup preserves the original failure if a secondary error occurs.

The clean build was RAM-loaded and mounted the 72 MiB container on the phone.
Managed boot-root → Super Fastboot → raw persist → Super Fastboot → boot-root
exports completed, with repeated read-only SCSI probes, synchronization, eject,
interface release and a final named fastboot query. Both boot-root samples
matched. Firmware logs recorded successful container mounts and export cleanup.
The probes issued no SCSI WRITE commands; no partition flash or deployment retry
was performed. One manually overlapped return-query/export attempt was excluded
from query validation; the subsequent serialized final query passed.

### Captured-image qualification, 2026-09-10

A host harness exercised the captured image without modifying it. It linked the
production Ext4 superblock, inode, directory, file, extent, checksum and image-map
code, together with its BaseLib and ordered-collection dependencies. The normal
EFI `OpenVolume` → `Open("\\efisp.fat", READ)` → `Read` path succeeded. Mapping
produced 71 extents covering the complete 72 MiB file; no allocation or filesystem
guard rejected it.

Before removing the duplicate checker, the same harness ran production
`SfbImageDiskInit` and `CheckFat`. Its parent
Block I/O enforced 4096-byte buffer alignment, block-aligned lengths and valid
device ranges. Both succeeded. Reading all 72 MiB through the virtual 512-byte
Block I/O reproduced the reviewed container's digest exactly. No parent writes
occurred. Opening and mapping required 12 outer-filesystem reads totalling
23,552 bytes; the subsequent FAT check and full-content comparison were separate
qualification operations.

Only platform services were substituted: allocation, CPU/memory primitives,
ASCII collation for this ASCII pathname, and read-only host-file disk I/O. The
harness did not simulate firmware protocol installation, driver connection/stop,
vendor storage implementations or USB. It therefore establishes that these exact
filesystem bytes, mapper guards and aligned disk translations do not reproduce
`EFI_INVALID_PARAMETER`; it does not establish a successful physical mount.
Private captures and the disposable harness remain outside the repository.

[ramdisk]: https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Universal/Disk/RamDiskDxe/RamDiskProtocol.c
[loop]: https://github.com/torvalds/linux/blob/master/drivers/block/loop.c
[grub]: https://chromium.googlesource.com/chromiumos/third_party/grub2/+/838dc6e4c569f6615aa9a391ce39df4c40118b80/disk/loopback.c
[connect]: https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Core/Dxe/Hand/DriverSupport.c
