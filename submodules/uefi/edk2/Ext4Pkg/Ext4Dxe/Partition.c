/**
  @file Driver entry point

  Copyright (c) 2021 Pedro Falcato All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "Ext4Dxe.h"

/**
   Opens an ext4 partition and installs the Simple File System protocol.

   @param[in]        DeviceHandle     Handle to the block device.
   @param[in]        DiskIo           Pointer to an EFI_DISK_IO_PROTOCOL.
   @param[in opt]    DiskIo2          Pointer to an EFI_DISK_IO2_PROTOCOL, if supported.
   @param[in]        BlockIo          Pointer to an EFI_BLOCK_IO_PROTOCOL.

   @retval EFI_SUCCESS      The opening was successful.
           !EFI_SUCCESS     Opening failed.
 */
EFI_STATUS
Ext4OpenPartition (
  IN EFI_HANDLE DeviceHandle,
  IN EFI_DISK_IO_PROTOCOL *DiskIo,
  IN OPTIONAL EFI_DISK_IO2_PROTOCOL *DiskIo2,
  IN EFI_BLOCK_IO_PROTOCOL *BlockIo
  )
{
  EXT4_PARTITION  *Part;
  EFI_STATUS      Status;

  Part = AllocateZeroPool (sizeof (*Part));

  if(Part == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Part->BlockIo = BlockIo;
  Part->DiskIo  = DiskIo;
  Part->DiskIo2 = DiskIo2;

  Status = Ext4OpenSuperblock (Part);

  if(EFI_ERROR (Status)) {
    FreePool (Part);
    return Status;
  }

  Part->Interface.Revision   = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION;
  Part->Interface.OpenVolume = Ext4OpenVolume;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &DeviceHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  &Part->Interface,
                  NULL
                  );

  if(EFI_ERROR (Status)) {
    FreePool (Part);
    return Status;
  }

  return EFI_SUCCESS;
}

/**
   Sets up the protocol and metadata of a file that is being opened.

   @param[in out]        File        Pointer to the file.
   @param[in]            Partition   Pointer to the opened partition.
 */
VOID
Ext4SetupFile (
  IN OUT EXT4_FILE *File, EXT4_PARTITION *Partition
  )
{
  // TODO: Support revision 2 (needs DISK_IO2 + asynchronous IO)
  File->Protocol.Revision    = EFI_FILE_PROTOCOL_REVISION;
  File->Protocol.Open        = Ext4Open;
  File->Protocol.Close       = Ext4Close;
  File->Protocol.Delete      = Ext4Delete;
  File->Protocol.Read        = Ext4ReadFile;
  File->Protocol.Write       = Ext4WriteFile;
  File->Protocol.SetPosition = Ext4SetPosition;
  File->Protocol.GetPosition = Ext4GetPosition;
  File->Protocol.GetInfo     = Ext4GetInfo;

  File->Partition = Partition;
}

/**
   Unmounts and frees an ext4 partition.

   @param[in]        Partition        Pointer to the opened partition.

   @retval Status of the unmount.
 */
EFI_STATUS
Ext4UnmountAndFreePartition (
  IN EXT4_PARTITION *Partition
  )
{
  Partition->Unmounting = TRUE;
  Ext4CloseInternal (Partition->Root);
  FreePool (Partition->BlockGroups);
  FreePool (Partition);

  return EFI_SUCCESS;
}
