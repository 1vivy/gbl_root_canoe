#ifndef __SUPER_FB_FAT_CLASSIFY_H__
#define __SUPER_FB_FAT_CLASSIFY_H__

#include "SuperFbMenu.h"

#define SFB_CLASSIFY_BPB_BYTES_PER_SEC   11
#define SFB_CLASSIFY_BPB_SEC_PER_CLUS    13
#define SFB_CLASSIFY_BPB_RSVD_SEC_CNT    14
#define SFB_CLASSIFY_BPB_NUM_FATS        16
#define SFB_CLASSIFY_BPB_ROOT_ENT_CNT    17
#define SFB_CLASSIFY_BPB_TOT_SEC_16      19
#define SFB_CLASSIFY_BPB_FAT_SZ_16       22
#define SFB_CLASSIFY_BPB_TOT_SEC_32      32
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

/*
 * TRUE when the bytes are the BIOS Parameter Block of a FAT file system of any
 * width. All three widths matter: this platform ships no FAT32 partition at
 * all - its 11 FAT volumes are FAT12 (bluetooth, dcp, logfs, soccp, spunvm)
 * and FAT16 (modem, qmcs) - and a small USB stick is routinely formatted
 * FAT16 by tools that pick a width from the medium size. Accepting only FAT32
 * made every one of them invisible to the volume scan while EnhancedFatDxe had
 * already mounted them.
 *
 * The width test is the one from the FAT specification: a FAT32 BPB has no
 * fixed root directory and carries its sizes in the 32-bit fields, a
 * FAT12/FAT16 BPB has a fixed root directory and a 16-bit FAT size. The
 * geometry checks around it are what keep the predicate from firing on the 163
 * non-FAT partitions that sit beside them.
 */
STATIC
BOOLEAN
SfbClassifyFat (IN CONST UINT8 *Bytes, IN UINTN Size)
{
  UINT16  BytesPerSec;
  UINT8   SecPerClus;
  UINT16  RootEntCnt;
  UINT16  FatSz16;
  UINT16  TotSec16;

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

  /* Sectors per cluster is a power of two, at most 128. */
  SecPerClus = Bytes[SFB_CLASSIFY_BPB_SEC_PER_CLUS];
  if (SecPerClus == 0 || SecPerClus > 128 ||
      (SecPerClus & (UINT8)(SecPerClus - 1)) != 0) {
    return FALSE;
  }

  /* Every FAT volume has at least one FAT and at least one reserved sector. */
  if (Bytes[SFB_CLASSIFY_BPB_NUM_FATS] == 0 ||
      SfbClassifyLe16 (Bytes, SFB_CLASSIFY_BPB_RSVD_SEC_CNT) == 0) {
    return FALSE;
  }

  RootEntCnt = SfbClassifyLe16 (Bytes, SFB_CLASSIFY_BPB_ROOT_ENT_CNT);
  FatSz16 = SfbClassifyLe16 (Bytes, SFB_CLASSIFY_BPB_FAT_SZ_16);
  TotSec16 = SfbClassifyLe16 (Bytes, SFB_CLASSIFY_BPB_TOT_SEC_16);

  /* FAT32: root directory is a cluster chain, sizes live in the 32-bit fields. */
  if (RootEntCnt == 0 && FatSz16 == 0 && TotSec16 == 0 &&
      SfbClassifyLe32 (Bytes, SFB_CLASSIFY_BPB_FAT_SZ_32) != 0) {
    return TRUE;
  }

  /* FAT12/FAT16: fixed root directory, 16-bit FAT size, either sector count. */
  return (BOOLEAN)(RootEntCnt != 0 && FatSz16 != 0 &&
                   (TotSec16 != 0 ||
                    SfbClassifyLe32 (Bytes, SFB_CLASSIFY_BPB_TOT_SEC_32) != 0));
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

/* FAT wins before ext4 is considered, so a coincidental 0xEF53 is harmless. */
STATIC
SFB_VOLUME_KIND
SfbClassifyVolumeBytes (IN CONST UINT8 *Bytes, IN UINTN Size)
{
  if (SfbClassifyFat (Bytes, Size)) {
    return SfbVolumeKindFat;
  }
  if (SfbClassifyExt4 (Bytes, Size)) {
    return SfbVolumeKindExt4;
  }
  return SfbVolumeKindOther;
}

#endif
