/**
  @file Inode related routines

  Copyright (c) 2021 Pedro Falcato All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "Ext4Dxe.h"

#include <Uefi.h>
#include <Library/TimeBaseLib.h>

/**
   Calculates the checksum of the given inode.
   @param[in]      Partition     Pointer to the opened EXT4 partition.
   @param[in]      Inode         Pointer to the inode.
   @param[in]      InodeNum      Inode number.

   @return The checksum.
*/
UINT32
Ext4CalculateInodeChecksum (
  IN CONST EXT4_PARTITION *Partition,
  IN CONST EXT4_INODE *Inode,
  IN EXT4_INO_NR InodeNum
  )
{
  UINT32      Crc;
  UINT16      Dummy;
  BOOLEAN     HasSecondChecksumField;
  CONST VOID  *RestOfInode;
  UINTN       RestOfInodeLength;

  HasSecondChecksumField = Ext4InodeHasField (Inode, i_checksum_hi);

  Dummy = 0;

  Crc = Ext4CalculateChecksum (Partition, &InodeNum, sizeof (InodeNum), Partition->InitialSeed);
  Crc = Ext4CalculateChecksum (Partition, &Inode->i_generation, sizeof (Inode->i_generation), Crc);

  Crc = Ext4CalculateChecksum (
          Partition,
          Inode,
          OFFSET_OF (EXT4_INODE, i_osd2.data_linux.l_i_checksum_lo),
          Crc
          );

  Crc = Ext4CalculateChecksum (Partition, &Dummy, sizeof (Dummy), Crc);

  RestOfInode = &Inode->i_osd2.data_linux.l_i_reserved;
  RestOfInodeLength = Partition->InodeSize - OFFSET_OF (EXT4_INODE, i_osd2.data_linux.l_i_reserved);

  if (HasSecondChecksumField) {
    UINTN  Length = OFFSET_OF (EXT4_INODE, i_checksum_hi) - OFFSET_OF (EXT4_INODE, i_osd2.data_linux.l_i_reserved);

    Crc = Ext4CalculateChecksum (Partition, &Inode->i_osd2.data_linux.l_i_reserved, Length, Crc);
    Crc = Ext4CalculateChecksum (Partition, &Dummy, sizeof (Dummy), Crc);

    // 4 is the size of the i_extra_size field + the size of i_checksum_hi
    RestOfInodeLength = Partition->InodeSize - EXT4_GOOD_OLD_INODE_SIZE - 4;
    RestOfInode = &Inode->i_ctime_extra;
  }

  Crc = Ext4CalculateChecksum (Partition, RestOfInode, RestOfInodeLength, Crc);

  return Crc;
}

/**
   Reads from an EXT4 inode.
   @param[in]      Partition     Pointer to the opened EXT4 partition.
   @param[in]      File          Pointer to the opened file.
   @param[out]     Buffer        Pointer to the buffer.
   @param[in]      Offset        Offset of the read.
   @param[in out]  Length        Pointer to the length of the buffer, in bytes.
                                 After a succesful read, it's updated to the number of read bytes.

   @return Status of the read operation.
*/
EFI_STATUS
Ext4Read (
  IN     EXT4_PARTITION *Partition,
  IN     EXT4_FILE *File,
  OUT    VOID *Buffer,
  IN     UINT64 Offset,
  IN OUT UINTN *Length
  )
{
  // DEBUG((EFI_D_INFO, "Ext4Read[Offset %lu, Length %lu]\n", Offset, *Length));
  EXT4_INODE  *Inode;
  UINT64      InodeSize;
  UINT64      CurrentSeek;
  UINTN       RemainingRead;
  UINTN       BeenRead;

  Inode         = File->Inode;
  InodeSize     = Ext4InodeSize (Inode);
  CurrentSeek   = Offset;
  RemainingRead = *Length;
  BeenRead      = 0;

  if(Offset > InodeSize) {
    return EFI_DEVICE_ERROR;
  }

  if (RemainingRead > InodeSize - Offset) {
    RemainingRead = (UINTN)(InodeSize - Offset);
  }

  while(RemainingRead != 0) {
    UINTN        WasRead;
    EXT4_EXTENT  Extent;
    UINT32       BlockOff;
    EFI_STATUS   Status;
    BOOLEAN      HasBackingExtent;

    WasRead = 0;

    // The algorithm here is to get the extent corresponding to the current block
    // and then read as much as we can from the current extent.

    Status = Ext4GetExtent (
               Partition,
               File,
               DivU64x32Remainder (CurrentSeek, Partition->BlockSize, &BlockOff),
               &Extent
               );

    if(Status != EFI_SUCCESS && Status != EFI_NO_MAPPING) {
      return Status;
    }

    HasBackingExtent = Status != EFI_NO_MAPPING;

    if(!HasBackingExtent) {
      UINT32  HoleOff;
      UINTN   HoleLen;

      HoleOff = BlockOff;
      HoleLen = Partition->BlockSize - HoleOff;
      WasRead = HoleLen > RemainingRead ? RemainingRead : HoleLen;
      // TODO: Get the hole size and memset all that
      SetMem (Buffer, WasRead, 0);
    } else {
      UINT64  ExtentStartBytes;
      UINT64  ExtentLengthBytes;
      UINT64  ExtentLogicalBytes;

      // Our extent offset is the difference between CurrentSeek and ExtentLogicalBytes
      UINT64  ExtentOffset;
      UINTN   ExtentMayRead;

      ExtentStartBytes   = (((UINT64)Extent.ee_start_hi << 32) | Extent.ee_start_lo) * Partition->BlockSize;
      ExtentLengthBytes  = Extent.ee_len * Partition->BlockSize;
      ExtentLogicalBytes = (UINT64)Extent.ee_block * Partition->BlockSize;
      ExtentOffset  = CurrentSeek - ExtentLogicalBytes;
      ExtentMayRead = (UINTN)(ExtentLengthBytes - ExtentOffset);

      WasRead = ExtentMayRead > RemainingRead ? RemainingRead : ExtentMayRead;

      // DEBUG((EFI_D_INFO, "[ext4] may read %lu, remaining %lu\n", ExtentMayRead, RemainingRead));
      // DEBUG((EFI_D_INFO, "[ext4] Reading block %lu\n", (ExtentStartBytes + ExtentOffset) / Partition->BlockSize));
      Status = Ext4ReadDiskIo (Partition, Buffer, WasRead, ExtentStartBytes + ExtentOffset);

      if(EFI_ERROR (Status)) {
        DEBUG ((
          EFI_D_ERROR,
          "[ext4] Error %x reading [%lu, %lu]\n",
          Status,
          ExtentStartBytes + ExtentOffset,
          ExtentStartBytes + ExtentOffset + WasRead - 1
          ));
        return Status;
      }
    }

    RemainingRead -= WasRead;
    Buffer       = (VOID *)((CHAR8 *)Buffer + WasRead);
    BeenRead    += WasRead;
    CurrentSeek += WasRead;
  }

  *Length = BeenRead;

  // DEBUG((EFI_D_INFO, "File length %lu crc %x\n", BeenRead, CalculateCrc32(original, BeenRead)));
  return EFI_SUCCESS;
}

/**
   Allocates a zeroed inode structure.
   @param[in]      Partition     Pointer to the opened EXT4 partition.

   @return Pointer to the allocated structure, from the pool,
           with size Partition->InodeSize.
*/
EXT4_INODE *
Ext4AllocateInode (
  IN EXT4_PARTITION *Partition
  )
{
  BOOLEAN     NeedsToZeroRest;
  UINT32      InodeSize;
  EXT4_INODE  *Inode;

  NeedsToZeroRest = FALSE;
  InodeSize = Partition->InodeSize;

  // HACK!: We allocate a structure of at least sizeof(EXT4_INODE), but in the future, when
  // write support is added and we need to flush inodes to disk, we could have a bit better
  // distinction between the on-disk inode and a separate, nicer to work with inode struct.
  if (InodeSize < sizeof (EXT4_INODE)) {
    InodeSize = sizeof (EXT4_INODE);
    NeedsToZeroRest = TRUE;
  }

  Inode = AllocateZeroPool (InodeSize);

  if (!Inode) {
    return NULL;
  }

  if (NeedsToZeroRest) {
    Inode->i_extra_isize = 0;
  }

  return Inode;
}

/**
   Checks if a file is a directory.
   @param[in]      File          Pointer to the opened file.

   @return TRUE if file is a directory.
*/
BOOLEAN
Ext4FileIsDir (
  IN CONST EXT4_FILE *File
  )
{
  return (File->Inode->i_mode & EXT4_INO_TYPE_DIR) == EXT4_INO_TYPE_DIR;
}

/**
   Checks if a file is a regular file.
   @param[in]      File          Pointer to the opened file.

   @return BOOLEAN         TRUE if file is a regular file.
*/
BOOLEAN
Ext4FileIsReg (
  IN CONST EXT4_FILE *File
  )
{
  return (File->Inode->i_mode & EXT4_INO_TYPE_REGFILE) == EXT4_INO_TYPE_REGFILE;
}

/**
   Calculates the physical space used by a file.
   @param[in]      File          Pointer to the opened file.

   @return Physical space used by a file, in bytes.
*/
UINT64
Ext4FilePhysicalSpace (
  EXT4_FILE *File
  )
{
  BOOLEAN  HugeFile;
  UINT64   Blocks;

  HugeFile = Ext4HasRoCompat (File->Partition, EXT4_FEATURE_RO_COMPAT_HUGE_FILE);
  Blocks   = File->Inode->i_blocks;

  if(HugeFile) {
    Blocks |= ((UINT64)File->Inode->i_osd2.data_linux.l_i_blocks_high) << 32;

    // If HUGE_FILE is enabled and EXT4_HUGE_FILE_FL is set in the inode's flags, each unit
    // in i_blocks corresponds to an actual filesystem block
    if(File->Inode->i_flags & EXT4_HUGE_FILE_FL) {
      return Blocks * File->Partition->BlockSize;
    }
  }

  // Else, each i_blocks unit corresponds to 512 bytes
  return Blocks * 512;
}

// The time format used to (de/en)code timestamp and timestamp_extra is documented on
// the ext4 docs page in kernel.org
#define EXT4_EXTRA_TIMESTAMP_MASK  ((1 << 2) - 1)

#define EXT4_FILE_GET_TIME_GENERIC(Name, Field)            \
  VOID \
  Ext4File ## Name (IN EXT4_FILE *File, OUT EFI_TIME *Time) \
  {                                                          \
    EXT4_INODE  *Inode = File->Inode;                       \
    UINT64      SecondsEpoch = Inode->Field;                   \
    UINT32      Nanoseconds  = 0;                                \
                                                           \
    if (Ext4InodeHasField (Inode, Field ## _extra)) {          \
      SecondsEpoch |= ((UINT64)(Inode->Field ## _extra & EXT4_EXTRA_TIMESTAMP_MASK)) << 32; \
      Nanoseconds   = Inode->Field ## _extra >> 2;                                            \
    }                                                                                       \
    EpochToEfiTime ((UINTN)SecondsEpoch, Time);                                                     \
    Time->Nanosecond = Nanoseconds;                                                         \
  }

// Note: EpochToEfiTime should be adjusted to take in a UINT64 instead of a UINTN, in order to avoid Y2038
// on 32-bit systems.

/**
   Gets the file's last access time.
   @param[in]      File   Pointer to the opened file.
   @param[out]     Time   Pointer to an EFI_TIME structure.
*/
EXT4_FILE_GET_TIME_GENERIC (ATime, i_atime);

/**
   Gets the file's last (data) modification time.
   @param[in]      File   Pointer to the opened file.
   @param[out]     Time   Pointer to an EFI_TIME structure.
*/
EXT4_FILE_GET_TIME_GENERIC (MTime, i_mtime);

/**
   Gets the file's creation time.
   @param[in]      File   Pointer to the opened file.
   @param[out]     Time   Pointer to an EFI_TIME structure.
*/
STATIC
EXT4_FILE_GET_TIME_GENERIC (
  CrTime, i_crtime
  );

/**
   Gets the file's creation time, if possible.
   @param[in]      File   Pointer to the opened file.
   @param[out]     Time   Pointer to an EFI_TIME structure.
                          In the case where the the creation time isn't recorded,
                          Time is zeroed.
*/
VOID
Ext4FileCreateTime (
  IN EXT4_FILE *File,
  OUT EFI_TIME *Time
  )
{
  EXT4_INODE  *Inode;

  Inode = File->Inode;

  if (!Ext4InodeHasField (Inode, i_crtime)) {
    SetMem (Time, sizeof (EFI_TIME), 0);
    return;
  }

  Ext4FileCrTime (File, Time);
}

/**
   Checks if the checksum of the inode is correct.
   @param[in]      Partition     Pointer to the opened EXT4 partition.
   @param[in]      Inode         Pointer to the inode.
   @param[in]      InodeNum      Inode number.

   @return TRUE if checksum is correct, FALSE if there is corruption.
*/
BOOLEAN
Ext4CheckInodeChecksum (
  IN CONST EXT4_PARTITION *Partition,
  IN CONST EXT4_INODE *Inode,
  IN EXT4_INO_NR InodeNum
  )
{
  UINT32  Csum;
  UINT32  DiskCsum;

  if(!Ext4HasMetadataCsum (Partition)) {
    return TRUE;
  }

  Csum = Ext4CalculateInodeChecksum (Partition, Inode, InodeNum);

  DiskCsum = Inode->i_osd2.data_linux.l_i_checksum_lo;

  if (Ext4InodeHasField (Inode, i_checksum_hi)) {
    DiskCsum |= ((UINT32)Inode->i_checksum_hi) << 16;
  } else {
    // Only keep the lower bits for the comparison if the checksum is 16 bits.
    Csum &= 0xffff;
  }

 #if 0
    DEBUG ((EFI_D_INFO, "[ext4] Inode %d csum %x vs %x\n", InodeNum, Csum, DiskCsum));
 #endif

  return Csum == DiskCsum;
}
