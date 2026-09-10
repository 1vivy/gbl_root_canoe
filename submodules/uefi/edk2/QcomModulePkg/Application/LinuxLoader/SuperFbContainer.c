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
STATIC BOOLEAN mUsbOwned;
STATIC EXT4_IMAGE_MAP *mMap;
STATIC EFI_HANDLE mHandle;
STATIC EFI_HANDLE mPersist;
STATIC EFI_DEVICE_PATH_PROTOCOL *mPath;
STATIC EFI_GUID mContainerGuid = {
    0xf1086281, 0xc184, 0x47f7, {0xbb, 0xae, 0x30, 0x61, 0x1f, 0x90, 0xe2, 0xa4}};

STATIC EFI_STATUS GetFileSystem (EFI_HANDLE Handle, EFI_SIMPLE_FILE_SYSTEM_PROTOCOL **Fs)
{
  EFI_STATUS Status, Connect;
  *Fs = NULL;
  Status = gBS->HandleProtocol (Handle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)Fs);
  if (!EFI_ERROR (Status) && *Fs != NULL)
    return EFI_SUCCESS;
  Connect = gBS->ConnectController (Handle, NULL, NULL, TRUE);
  Status = gBS->HandleProtocol (Handle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)Fs);
  /* ConnectController reports whether a driver was newly started. An already
   * published filesystem is the result we need, including after a reconnect. */
  if (!EFI_ERROR (Status) && *Fs != NULL)
    return EFI_SUCCESS;
  return EFI_ERROR (Connect) ? Connect : (EFI_ERROR (Status) ? Status : EFI_NOT_FOUND);
}

BOOLEAN SfbContainerMatchesIdentity (CONST CHAR8 *Token)
{
  STATIC CONST CHAR8 Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  UINTN I, O = 0;
  if (Token == NULL || mUsbOwned || mMap == NULL || mHandle == NULL)
    return FALSE;
  for (I = 0; I < sizeof (mMap->Identity); I += 3)
  {
    UINT32 V = ((UINT32)mMap->Identity[I] << 16) |
               ((UINT32)mMap->Identity[I + 1] << 8) | mMap->Identity[I + 2];
    UINTN J;
    for (J = 0; J < 4; J++, O++)
      if (Token[O] != Alphabet[(V >> (18 - J * 6)) & 63])
        return FALSE;
  }
  return Token[O] == '\0';
}

BOOLEAN SfbIsContainerVolume (EFI_HANDLE Handle)
{
  return !mUsbOwned && mHandle != NULL && Handle == mHandle && mDisk.Active;
}
STATIC EFI_STATUS UnmountInternal (VOID)
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
    Status = Ext4ReleaseImageFileSystem (mPersist);
    if (EFI_ERROR (Status) && Status != EFI_NOT_FOUND)
      return Status;
    mPersist = NULL;
  }
  return EFI_SUCCESS;
}

EFI_STATUS SfbContainerMount (VOID)
{
  EFI_STATUS Status;
  CONST CHAR8 *Stage = "persist-connect";
  EFI_BLOCK_IO_PROTOCOL *Parent = NULL;
  EFI_HANDLE *Handles = NULL;
  UINTN Count = 0, I;
  EFI_FILE_PROTOCOL *Root = NULL, *File = NULL;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
  EFI_DEVICE_PATH_PROTOCOL *ParentPath;
  VENDOR_DEVICE_PATH Node;
  if (mUsbOwned)
    return EFI_ACCESS_DENIED;
  if (mHandle != NULL)
  {
    /* A prior failed flush may have disconnected FAT but retained the disk. */
    return GetFileSystem (mHandle, &Fs);
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
  Stage = "persist-filesystem";
  Status = Ext4OpenImageFileSystem (mPersist, &Fs);
  if (EFI_ERROR (Status) || Fs == NULL)
    goto Failed;
  Stage = "persist-root";
  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status))
    goto Failed;
  Stage = "container-open";
  Status = Root->Open (Root, &File, L"\\efisp.fat", EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status))
    goto Failed;
  Stage = "container-map";
  Status = Ext4MapImage (File, &mMap);
  File->Close (File);
  File = NULL;
  Root->Close (Root);
  Root = NULL;
  if (EFI_ERROR (Status))
    goto Failed;
  Stage = "image-disk";
  Status = SfbImageDiskInit (&mDisk, mMap);
  if (EFI_ERROR (Status))
    goto Failed;
  Stage = "persist-path";
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
  Stage = "container-path";
  mPath = AppendDevicePathNode (ParentPath, &Node.Header);
  if (mPath == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Failed;
  }
  Stage = "container-publish";
  Status = gBS->InstallMultipleProtocolInterfaces (&mHandle, &gEfiBlockIoProtocolGuid, &mDisk.Block,
                                                   &gEfiDevicePathProtocolGuid, mPath, NULL);
  if (EFI_ERROR (Status))
    goto Failed;
  Stage = "fat-filesystem";
  Status = GetFileSystem (mHandle, &Fs);
  if (EFI_ERROR (Status) || Fs == NULL)
    goto Failed;
  DEBUG ((EFI_D_INFO, "SFB: MARK container mounted=1 bytes=%Lu\n", mMap->Bytes));
  return EFI_SUCCESS;
Failed:
  (VOID) Stage;
  DEBUG ((EFI_D_ERROR, "SFB: MARK container-mount stage=%a status=%r\n", Stage, Status));
  if (File != NULL)
    File->Close (File);
  if (Root != NULL)
    Root->Close (Root);
  {
    EFI_STATUS Cleanup = SfbContainerUnmount ();
    if (EFI_ERROR (Cleanup))
    {
      DEBUG ((EFI_D_ERROR, "SFB: MARK container-cleanup status=%r\n", Cleanup));
      /* A cleanup failure must not replace the filesystem error that caused
       * cleanup. Retained handles still make the next handoff retry cleanup. */
      return EFI_ERROR (Status) ? Status : Cleanup;
    }
  }
  return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;
}

EFI_STATUS SfbContainerOpenRoot (EFI_FILE_PROTOCOL **Root)
{
  EFI_STATUS Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
  if (Root == NULL) return EFI_INVALID_PARAMETER;
  *Root = NULL;
  Status = SfbContainerMount ();
  if (EFI_ERROR (Status)) return Status;
  if (mUsbOwned || mHandle == NULL || gBS == NULL || gBS->HandleProtocol == NULL)
    return EFI_NOT_READY;
  Status = gBS->HandleProtocol (mHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR (Status) || Fs == NULL || Fs->OpenVolume == NULL)
    return EFI_ERROR (Status) ? Status : EFI_NOT_READY;
  Status = Fs->OpenVolume (Fs, Root);
  return EFI_ERROR (Status) ? Status : (*Root != NULL ? EFI_SUCCESS : EFI_DEVICE_ERROR);
}

EFI_STATUS SfbContainerFlush (VOID)
{
  if (mUsbOwned || mHandle == NULL || !mDisk.Active || mDisk.Block.FlushBlocks == NULL)
    return EFI_NOT_READY;
  return mDisk.Block.FlushBlocks (&mDisk.Block);
}

EFI_BLOCK_IO_PROTOCOL *SfbContainerDisplayDisk (VOID)
{
  return !mUsbOwned && mHandle != NULL ? &mDisk.Block : NULL;
}

EFI_STATUS SfbContainerUsbBegin (EFI_BLOCK_IO_PROTOCOL **Disk)
{
  return SfbContainerUsbBeginBound (Disk, NULL);
}

EFI_STATUS SfbContainerUsbBeginBound (EFI_BLOCK_IO_PROTOCOL **Disk, CONST CHAR8 *Identity)
{
  EFI_STATUS Status;
  if (Disk == NULL)
    return EFI_INVALID_PARAMETER;
  *Disk = NULL;
  if (mUsbOwned)
    return EFI_ACCESS_DENIED;
  Status = SfbContainerMount ();
  if (EFI_ERROR (Status))
    return Status;
  /* Check the map actually handed to USB, after any host-controller reset.
   * An earlier fastboot preflight may have inspected a discarded map. */
  if (Identity != NULL && !SfbContainerMatchesIdentity (Identity))
    return EFI_ACCESS_DENIED;
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
  FreePool (mPath);
  mPath = NULL;
  mUsbOwned = TRUE;
  *Disk = &mDisk.Block;
  return EFI_SUCCESS;
}

EFI_STATUS SfbContainerUsbEnd (BOOLEAN GadgetReleased)
{
  EFI_STATUS Status;
  if (!mUsbOwned)
    return EFI_NOT_STARTED;
  if (!GadgetReleased)
    return EFI_ACCESS_DENIED;
  if (mMap != NULL)
  {
    Status = mDisk.Block.FlushBlocks (&mDisk.Block);
    if (EFI_ERROR (Status)) return Status;
  }
  Status = UnmountInternal ();
  if (!EFI_ERROR (Status)) mUsbOwned = FALSE;
  return Status;
}

EFI_STATUS SfbContainerUnmount (VOID)
{
  if (mUsbOwned) return EFI_ACCESS_DENIED;
  return UnmountInternal ();
}
