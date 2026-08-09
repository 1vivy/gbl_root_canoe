/**
  @file Disk utilities

  Copyright (c) 2021 Pedro Falcato All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "Ext4Dxe.h"

/**
   Reads from the partition's disk using the DISK_IO protocol.

   @param[in]  Partition      Pointer to the opened ext4 partition.
   @param[out] Buffer         Pointer to a destination buffer.
   @param[in]  Length         Length of the destination buffer.
   @param[in]  Offset         Offset, in bytes, of the location to read.

   @return Success status of the disk read.
 */
EFI_STATUS
Ext4ReadDiskIo (
  IN EXT4_PARTITION *Partition,
  OUT VOID *Buffer,
  IN UINTN Length,
  IN UINT64 Offset
  )
{
  return Ext4DiskIo (Partition)->ReadDisk (Ext4DiskIo (Partition), Ext4MediaId (Partition), Offset, Length, Buffer);
}

/**
   Reads blocks from the partition's disk using the DISK_IO protocol.

   @param[in]  Partition      Pointer to the opened ext4 partition.
   @param[out] Buffer         Pointer to a destination buffer.
   @param[in]  NumberBlocks   Length of the read, in filesystem blocks.
   @param[in]  BlockNumber    Starting block number.

   @return Success status of the read.
 */
EFI_STATUS
Ext4ReadBlocks (
  IN EXT4_PARTITION *Partition,
  OUT VOID *Buffer,
  IN UINTN NumberBlocks,
  IN EXT4_BLOCK_NR BlockNumber
  )
{
  return Ext4ReadDiskIo (Partition, Buffer, NumberBlocks * Partition->BlockSize, BlockNumber * Partition->BlockSize);
}

/**
   Allocates a buffer and reads blocks from the partition's disk using the DISK_IO protocol.
   This function is deprecated and will be removed in the future.

   @param[in]  Partition      Pointer to the opened ext4 partition.
   @param[in]  NumberBlocks   Length of the read, in filesystem blocks.
   @param[in]  BlockNumber    Starting block number.

   @return Buffer allocated by AllocatePool, or NULL if some part of the process
           failed.
 */
VOID *
Ext4AllocAndReadBlocks (
  IN EXT4_PARTITION *Partition,
  IN UINTN NumberBlocks,
  IN EXT4_BLOCK_NR BlockNumber
  )
{
  VOID  *Buf;

  Buf = AllocatePool (NumberBlocks * Partition->BlockSize);

  if(Buf == NULL) {
    return NULL;
  }

  if(Ext4ReadBlocks (Partition, Buf, NumberBlocks, BlockNumber) != EFI_SUCCESS) {
    FreePool (Buf);
    return NULL;
  }

  return Buf;
}
