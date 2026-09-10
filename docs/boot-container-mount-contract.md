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

There is also avoidable duplication. `CheckFat` in `SuperFbContainer.c` parses
the BPB, insists on the provisioner's precise FAT16 geometry and compares both
FAT copies before connecting FatDxe. That is filesystem policy in the mount
wrapper. The intended simplification is to let FatDxe determine whether the
exposed disk is mountable; keep provisioning size policy at creation time and
physical-range safety in the image disk. Do not add repeated whole-image reads,
hash gates or read-only remount cycles to ordinary mounted-file operations.

Filesystem close/flush and USB handoff are distinct operations. Finish and
synchronize writes before giving another owner the disk; disconnect cached
filesystem views before their backing storage can change. A flush failure must
remain visible, and a rejected export must not be reported as an active USB
storage session.

## Current diagnostic boundary

The retained post-deployment image contains a valid, empty FAT16 container whose
bytes match the completed provisioning receipt. The CRC16 backport admits its
outer ext4 filesystem, but physical BDS mounting still reported
`EFI_INVALID_PARAMETER`. This does not identify a corrupt filesystem or justify
relaxing unrelated checks. Mount-stage logging now distinguishes opening the
outer filesystem, opening/mapping the image, publishing Block I/O and connecting
the FAT driver. Physical confirmation of that stage is still needed.

[ramdisk]: https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Universal/Disk/RamDiskDxe/RamDiskProtocol.c
[loop]: https://github.com/torvalds/linux/blob/master/drivers/block/loop.c
[grub]: https://chromium.googlesource.com/chromiumos/third_party/grub2/+/838dc6e4c569f6615aa9a391ce39df4c40118b80/disk/loopback.c
