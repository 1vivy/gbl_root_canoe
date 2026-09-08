/* The owned FAT view of persist/efisp.fat. No legacy directory fallback.
 * SPDX-License-Identifier: BSD-3-Clause */
#include "SuperFbContainer.h"
#include "SuperFbImageDisk.h"
#include "SuperFbMenu.h"
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/DevicePath.h>

STATIC SFB_IMAGE_DISK mDisk;
STATIC EXT4_IMAGE_MAP *mMap;
STATIC EFI_HANDLE mHandle;
STATIC EFI_HANDLE mPersist;
STATIC EFI_DEVICE_PATH_PROTOCOL *mPath;
STATIC EFI_GUID mContainerGuid = {
    0xf1086281, 0xc184, 0x47f7, {0xbb, 0xae, 0x30, 0x61, 0x1f, 0x90, 0xe2, 0xa4}};

BOOLEAN SfbIsContainerVolume (EFI_HANDLE Handle)
{
  return mHandle != NULL && Handle == mHandle && mDisk.Active;
}
STATIC UINT16 Le16 (CONST UINT8 *P) { return (UINT16)(P[0] | (P[1] << 8)); }
STATIC UINT32 Le32 (CONST UINT8 *P) { return (UINT32)Le16 (P) | ((UINT32)Le16 (P + 2) << 16); }
STATIC EFI_STATUS CheckFat (VOID)
{
  UINT8 Header[512], Mirror[512];
  UINTN Sector;
  EFI_STATUS Status =
      mDisk.Block.ReadBlocks (&mDisk.Block, mDisk.Media.MediaId, 0, sizeof (Header), Header);
  if (EFI_ERROR (Status))
    return Status;
  if (Le16 (Header + 11) != 512 || Header[13] != 4 || Le16 (Header + 14) != 1 || Header[16] != 2 ||
      Le16 (Header + 17) != 512 || Le16 (Header + 19) != 0 || Le16 (Header + 22) != 64 ||
      Le32 (Header + 32) != 65536 || Le16 (Header + 510) != 0xaa55)
    return EFI_VOLUME_CORRUPTED;
  Status = mDisk.Block.ReadBlocks (&mDisk.Block, mDisk.Media.MediaId, 1, sizeof (Header), Header);
  if (EFI_ERROR (Status))
    return Status;
  if (Le16 (Header) != 0xfff8 || (Le16 (Header + 2) & 0xc000) != 0xc000)
    return EFI_VOLUME_CORRUPTED;
  for (Sector = 0; Sector < 64; Sector++)
  {
    Status = mDisk.Block.ReadBlocks (&mDisk.Block, mDisk.Media.MediaId, 1 + Sector, sizeof (Header),
                                     Header);
    if (EFI_ERROR (Status))
      return Status;
    Status = mDisk.Block.ReadBlocks (&mDisk.Block, mDisk.Media.MediaId, 65 + Sector,
                                     sizeof (Mirror), Mirror);
    if (EFI_ERROR (Status))
      return Status;
    if (CompareMem (Header, Mirror, sizeof (Header)) != 0)
      return EFI_VOLUME_CORRUPTED;
  }
  return EFI_SUCCESS;
}

EFI_STATUS SfbContainerUnmount (VOID)
{
  EFI_STATUS Status;
  if (mHandle != NULL)
  {
    /* Driver stop must release its FAT caches and open files before storage
     * can be handed to USB. A refusal leaves the view alive and blocks export. */
    Status = gBS->DisconnectController (mHandle, NULL, NULL);
    if (EFI_ERROR (Status) && Status != EFI_NOT_FOUND)
      return Status;
    Status = mDisk.Block.FlushBlocks (&mDisk.Block);
    if (EFI_ERROR (Status))
      return Status;
    Status = gBS->UninstallMultipleProtocolInterfaces (
        mHandle, &gEfiBlockIoProtocolGuid, &mDisk.Block, &gEfiDevicePathProtocolGuid, mPath, NULL);
    if (EFI_ERROR (Status))
      return Status;
    mHandle = NULL;
  }
  if (mMap != NULL)
  {
    SfbImageDiskDestroy (&mDisk);
    FreePool (mMap);
    mMap = NULL;
  }
  if (mPath != NULL)
  {
    FreePool (mPath);
    mPath = NULL;
  }
  if (mPersist != NULL)
  {
    /* Invalidate the read-only ext4 driver's cached inode/superblock before a
     * host can allocate or remove the container through a persist export. */
    Status = gBS->DisconnectController (mPersist, NULL, NULL);
    if (EFI_ERROR (Status) && Status != EFI_NOT_FOUND)
      return Status;
    mPersist = NULL;
  }
  return EFI_SUCCESS;
}

EFI_STATUS SfbContainerMount (VOID)
{
  EFI_STATUS Status;
  EFI_BLOCK_IO_PROTOCOL *Parent = NULL;
  EFI_HANDLE *Handles = NULL;
  UINTN Count = 0, I;
  EFI_FILE_PROTOCOL *Root = NULL, *File = NULL;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
  EFI_DEVICE_PATH_PROTOCOL *ParentPath;
  VENDOR_DEVICE_PATH Node;
  if (mHandle != NULL)
  {
    Status = gBS->HandleProtocol (mHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
    if (!EFI_ERROR (Status) && Fs != NULL)
      return EFI_SUCCESS;
    /* A prior failed flush may have disconnected FAT but retained the disk. */
    Status = gBS->ConnectController (mHandle, NULL, NULL, TRUE);
    if (EFI_ERROR (Status))
      return Status;
    return gBS->HandleProtocol (mHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  }
  if (mMap != NULL || mPath != NULL)
  {
    Status = SfbContainerUnmount ();
    if (EFI_ERROR (Status))
      return Status;
  }
  Status = SfbFindPartitionByName (L"persist", &Parent);
  if (EFI_ERROR (Status) || Parent == NULL)
    return EFI_NOT_FOUND;
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status))
    return Status;
  for (I = 0; I < Count; I++)
  {
    EFI_BLOCK_IO_PROTOCOL *Candidate = NULL;
    if (!EFI_ERROR (
            gBS->HandleProtocol (Handles[I], &gEfiBlockIoProtocolGuid, (VOID **)&Candidate)) &&
        Candidate == Parent)
    {
      mPersist = Handles[I];
      break;
    }
  }
  FreePool (Handles);
  if (mPersist == NULL)
    return EFI_NOT_FOUND;
  Status = gBS->ConnectController (mPersist, NULL, NULL, TRUE);
  if (EFI_ERROR (Status) && Status != EFI_ALREADY_STARTED)
    goto Failed;
  Status = gBS->HandleProtocol (mPersist, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR (Status) || Fs == NULL)
    goto Failed;
  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status))
    goto Failed;
  Status = Root->Open (Root, &File, L"\\efisp.fat", EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status))
    goto Failed;
  Status = Ext4MapImage (File, &mMap);
  File->Close (File);
  File = NULL;
  Root->Close (Root);
  Root = NULL;
  if (EFI_ERROR (Status))
    goto Failed;
  Status = SfbImageDiskInit (&mDisk, mMap);
  if (EFI_ERROR (Status))
    goto Failed;
  Status = CheckFat ();
  if (EFI_ERROR (Status))
    goto Failed;
  ParentPath = DevicePathFromHandle (mPersist);
  if (ParentPath == NULL)
  {
    Status = EFI_NOT_FOUND;
    goto Failed;
  }
  ZeroMem (&Node, sizeof (Node));
  Node.Header.Type = MEDIA_DEVICE_PATH;
  Node.Header.SubType = MEDIA_VENDOR_DP;
  SetDevicePathNodeLength (&Node.Header, sizeof (Node));
  CopyMem (&Node.Guid, &mContainerGuid, sizeof (Node.Guid));
  mPath = AppendDevicePathNode (ParentPath, &Node.Header);
  if (mPath == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Failed;
  }
  Status = gBS->InstallMultipleProtocolInterfaces (&mHandle, &gEfiBlockIoProtocolGuid, &mDisk.Block,
                                                   &gEfiDevicePathProtocolGuid, mPath, NULL);
  if (EFI_ERROR (Status))
    goto Failed;
  Status = gBS->ConnectController (mHandle, NULL, NULL, TRUE);
  if (EFI_ERROR (Status))
    goto Failed;
  Status = gBS->HandleProtocol (mHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR (Status) || Fs == NULL)
    goto Failed;
  DEBUG ((EFI_D_INFO, "SFB: MARK container mounted=1 bytes=%u\n", EXT4_IMAGE_BYTES));
  return EFI_SUCCESS;
Failed:
  if (File != NULL)
    File->Close (File);
  if (Root != NULL)
    Root->Close (Root);
  {
    EFI_STATUS Cleanup = SfbContainerUnmount ();
    if (EFI_ERROR (Cleanup))
      return Cleanup;
  }
  return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;
}
