#ifndef __SUPER_FB_FAT_CLASSIFY_H__
#define __SUPER_FB_FAT_CLASSIFY_H__

#include "SuperFbMenu.h"

#define SFB_CLASSIFY_BPB_BYTES_PER_SEC   11
#define SFB_CLASSIFY_BPB_ROOT_ENT_CNT    17
#define SFB_CLASSIFY_BPB_TOT_SEC_16      19
#define SFB_CLASSIFY_BPB_FAT_SZ_16       22
#define SFB_CLASSIFY_BPB_FAT_SZ_32       36
#define SFB_CLASSIFY_BPB_SIGNATURE       510
#define SFB_CLASSIFY_EXT4_SB_OFFSET      1024
#define SFB_CLASSIFY_EXT4_INODES         (SFB_CLASSIFY_EXT4_SB_OFFSET + 0)
#define SFB_CLASSIFY_EXT4_BLOCKS_LO      (SFB_CLASSIFY_EXT4_SB_OFFSET + 4)
#define SFB_CLASSIFY_EXT4_LOG_BLOCK_SIZE (SFB_CLASSIFY_EXT4_SB_OFFSET + 24)
#define SFB_CLASSIFY_EXT4_MAGIC          (SFB_CLASSIFY_EXT4_SB_OFFSET + 56)
#define SFB_CLASSIFY_EXT4_INODE_SIZE     (SFB_CLASSIFY_EXT4_SB_OFFSET + 88)

STATIC
UINT16
SfbClassifyLe16 (IN CONST UINT8 *Bytes, IN UINTN Offset)
{
  return (UINT16)(Bytes[Offset] | ((UINT16)Bytes[Offset + 1] << 8));
}

STATIC
UINT32
SfbClassifyLe32 (IN CONST UINT8 *Bytes, IN UINTN Offset)
{
  return (UINT32)Bytes[Offset] |
         ((UINT32)Bytes[Offset + 1] << 8) |
         ((UINT32)Bytes[Offset + 2] << 16) |
         ((UINT32)Bytes[Offset + 3] << 24);
}

STATIC
BOOLEAN
SfbClassifyFat32 (IN CONST UINT8 *Bytes, IN UINTN Size)
{
  UINT16 BytesPerSec;

  if (Bytes == NULL || Size < SFB_CLASSIFY_BPB_SIGNATURE + 2 ||
      Bytes[SFB_CLASSIFY_BPB_SIGNATURE] != 0x55 ||
      Bytes[SFB_CLASSIFY_BPB_SIGNATURE + 1] != 0xAA) {
    return FALSE;
  }

  BytesPerSec = SfbClassifyLe16 (Bytes, SFB_CLASSIFY_BPB_BYTES_PER_SEC);
  if (BytesPerSec != 512 && BytesPerSec != 1024 &&
      BytesPerSec != 2048 && BytesPerSec != 4096) {
    return FALSE;
  }

  return (BOOLEAN)(SfbClassifyLe16 (Bytes, SFB_CLASSIFY_BPB_ROOT_ENT_CNT) == 0 &&
                   SfbClassifyLe16 (Bytes, SFB_CLASSIFY_BPB_FAT_SZ_16) == 0 &&
                   SfbClassifyLe16 (Bytes, SFB_CLASSIFY_BPB_TOT_SEC_16) == 0 &&
                   SfbClassifyLe32 (Bytes, SFB_CLASSIFY_BPB_FAT_SZ_32) != 0);
}

STATIC
BOOLEAN
SfbClassifyExt4 (IN CONST UINT8 *Bytes, IN UINTN Size)
{
  UINT32 LogBlockSize;
  UINT16 InodeSize;

  if (Bytes == NULL || Size < SFB_CLASSIFY_EXT4_INODE_SIZE + 2 ||
      SfbClassifyLe16 (Bytes, SFB_CLASSIFY_EXT4_MAGIC) != 0xEF53 ||
      SfbClassifyLe32 (Bytes, SFB_CLASSIFY_EXT4_INODES) == 0 ||
      SfbClassifyLe32 (Bytes, SFB_CLASSIFY_EXT4_BLOCKS_LO) == 0) {
    return FALSE;
  }

  LogBlockSize = SfbClassifyLe32 (Bytes, SFB_CLASSIFY_EXT4_LOG_BLOCK_SIZE);
  InodeSize = SfbClassifyLe16 (Bytes, SFB_CLASSIFY_EXT4_INODE_SIZE);
  return (BOOLEAN)(LogBlockSize <= 6 && InodeSize >= 128 &&
                   (InodeSize & 3) == 0 &&
                   InodeSize <= (1024u << LogBlockSize));
}

/* FAT32 wins before ext4 is considered, so a coincidental 0xEF53 is harmless. */
STATIC
SFB_VOLUME_KIND
SfbClassifyVolumeBytes (IN CONST UINT8 *Bytes, IN UINTN Size)
{
  if (SfbClassifyFat32 (Bytes, Size)) {
    return SfbVolumeKindFat32;
  }
  if (SfbClassifyExt4 (Bytes, Size)) {
    return SfbVolumeKindExt4;
  }
  return SfbVolumeKindOther;
}

#endif
