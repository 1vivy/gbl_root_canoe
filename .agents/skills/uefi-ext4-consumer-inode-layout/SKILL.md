---
name: uefi-ext4-consumer-inode-layout
description: "Firmware ext4 read returns Unsupported or Not Found (LoadImage wraps it) although the bytes are correct: the host writer created block-mapped instead of extent-mapped inodes."
---

# UEFI ext4 consumer versus inode layout

## Use when

Use when Canoe/EDK2 finds an ext4 volume or menu row but cannot read `canoe.cfg`, a sidecar, or an image while Linux and the writer read identical bytes. The characteristic split is directory/path `Open` success followed by file-read `EFI_UNSUPPORTED` or `LoadImage` surfaced as `EFI_NOT_FOUND`.

## Procedure

1. Collect all evidence layers before concluding:

   | Layer | What it proves |
   | --- | --- |
   | Mounted persist listing/hash | installer bytes landed |
   | install receipt/generation | a commit happened |
   | `getvar canoe-boot-root` | observation frozen at BDS boot; not same-session state |
   | current `bds-N.log` | what the firmware reader actually observed |

2. Harvest logfs and read the sequence:

   ```bash
   grep -e 'boot-root' -e 'canoe.cfg' -e 'tzmap-load' -e 'image-load' -e 'launch ' /run/media/*/LOGFS/canoe/bds-*.log
   ```

   `populated-managed` proves name/open only. `canoe.cfg unavailable: Unsupported`, `config-valid=0`, `tzmap-load ... Unsupported fallback=builtin`, and `image-load ... Not Found` together indicate read capability, not missing bytes.

3. Inspect the inode, not file contents:

   ```bash
   debugfs -R "stat /path/file" image.img | grep -E 'Flags|EXTENTS|BLOCKS'
   ```

   `Flags: 0x80000` plus `EXTENTS:` is firmware-readable; `Flags: 0x0` plus `BLOCKS:` is rejected by EDK2 Ext4Dxe.

4. Understand the asymmetry. `Ext4Pkg/Ext4Dxe/Extents.c` returns `EFI_UNSUPPORTED` unless `EXT4_EXTENTS_FL` is set. `ext2fs_mkdir2` sets extents on directories, so lookup succeeds. A hand-built inode using `ext2fs_new_inode`, `ext2fs_write_new_inode`, and `ext2fs_file_write` remains block-mapped unless the writer initializes extents.

5. Reproduce A/B offline in one image:

   ```bash
   d=$(mktemp -d); img=$d/t.img
   dd if=/dev/zero of=$img bs=1M count=32 status=none
   mkfs.ext4 -q -F $img
   printf data > $d/f
   canoe-ext4 write $img /helper.bin < $d/f
   sudo mount -o loop $img $d/mnt
   printf data | sudo tee $d/mnt/kernel.bin >/dev/null
   sudo umount $d/mnt
   debugfs -R 'stat /helper.bin' $img | grep -i -e Flags -e EXTENTS -e BLOCKS
   debugfs -R 'stat /kernel.bin' $img | grep -i -e Flags -e EXTENTS
   ```

6. Fix creation in the libext2fs writer when the superblock has extents:

   ```c
   struct ext3_extent_header *header = (struct ext3_extent_header *)&inode.i_block[0];
   memset(inode.i_block, 0, sizeof(inode.i_block));
   header->eh_magic = ext2fs_cpu_to_le16(EXT3_EXT_MAGIC);
   header->eh_depth = 0;
   header->eh_entries = 0;
   header->eh_max = ext2fs_cpu_to_le16((sizeof(inode.i_block) - sizeof(*header)) / sizeof(struct ext3_extent));
   inode.i_flags |= EXT4_EXTENTS_FL;
   ```

   Include `<ext2fs/ext3_extents.h>` and guard with `ext2fs_has_feature_extents(fs->super)`.

7. Convert existing block-mapped files on overwrite: free blocks with `ext2fs_block_iterate3` and a freeing callback; zero `i_size` and `i_blocks`; install the empty extent header; write the inode; then write data. A normal overwrite preserves the bad mapping.

8. Skip `EXT4_INLINE_DATA_FL` inodes. Their data lives in the inode, and firmware generally refuses inline-data volumes at mount.

9. Rewrite already-installed files after fixing the writer. Code alone does not change existing inode layouts.

10. Build a deterministic legacy regression fixture:

   ```bash
   mkfs.ext4 -q -F -O ^extent,^64bit -b 1024 legacy.img
   <writer> write legacy.img /file < payload
   tune2fs -O extent legacy.img
   <writer> write legacy.img /file < payload2
   e2fsck -fn legacy.img
   ```

## Traps / failure signatures

- Byte-identical read-back and clean `e2fsck` prove content, not reader-compatible structure.
- `LoadImage ... Not Found` can wrap an underlying ext4 `EFI_UNSUPPORTED`.
- `populated-managed` means `Open` succeeded, not that bytes were read.
- Host kernel mounts support block maps, so a perfect host listing says nothing about Ext4Dxe.
- `debugfs write` creates extent-mapped files; it cannot seed the legacy shape.
- `mkfs.ext4 -O ^extent` fails while 64-bit remains enabled; disable both with `^extent,^64bit`.
- `bds-N.log` timestamps are session-relative milliseconds. Order within a file, never across files.
- The same structural-subset problem can affect FAT long names, sparse/compressed/inline files, and hashed directories.
- `tools/canoe-ext4` (libext2fs) is retained for reference and excluded from release and default checks; the packaged writer is the Rust stack, so verify the inode layout of whichever writer actually produced the image and of every other writer that shares the volume.
- Cross-built Windows writers need separate Wine + `debugfs` proof; `objdump -p` should show only system DLLs.

## Verification

Assert inode flags, not bytes. The pre-fix writer must leave the deterministic legacy file block-mapped; the fixed overwrite must remap it to `0x8xxxx` with `EXTENTS:` and keep `e2fsck -fn` clean. On device, a freshly rewritten file must lose `Unsupported`/`Not Found` and produce a successful config/sidecar/image read in the same session log.