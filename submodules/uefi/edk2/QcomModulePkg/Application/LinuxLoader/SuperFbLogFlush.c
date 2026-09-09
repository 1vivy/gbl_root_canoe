/*
 * Persist the Canoe BDS log.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/SimpleFileSystem.h>

#include "SuperFbGptName.h"
#include "SuperFbLog.h"

/* File-local since the platform-ring writer that shared them was removed. */
STATIC
EFI_STATUS
SfbWriteBytes (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST VOID        *Buffer,
  IN UINTN              Size
  )
{
  EFI_STATUS Status;
  UINTN      Written;
  if (File == NULL || (Buffer == NULL && Size != 0)) {
    return EFI_INVALID_PARAMETER;
  }
  if (Size == 0) {
    return EFI_SUCCESS;
  }
  Written = Size;
  Status = File->Write (File, &Written, (VOID *)Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  return (Written == Size) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

STATIC
EFI_STATUS
SfbWriteAscii (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST CHAR8       *Text
  )
{
  return SfbWriteBytes (File, Text, AsciiStrLen (Text));
}

STATIC
EFI_STATUS
SfbOpenLogfsRoot (
  OUT EFI_FILE_PROTOCOL **Root
  )
{
  EFI_STATUS                       Status;
  EFI_STATUS                       LastStatus;
  EFI_HANDLE                      *Handles;
  EFI_PARTITION_ENTRY             *PartEntry;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  UINTN                            Count;
  UINTN                            Index;

  if (Root == NULL || gBS == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Root = NULL;
  Handles = NULL;
  Count = 0;
  /* This allocates, and that is not the contradiction it looks like. The ring
     is allocation-free so capture survives anything; persisting is a different
     job that cannot be, because OpenVolume and Write allocate inside the FAT
     driver whatever this call does. Swapping in a static buffer and LocateHandle
     would remove the visible AllocatePool and change nothing about whether a
     flush can run without a heap.

     What earns the risk is that failing here costs nothing: the caller releases
     the borrow, the ring keeps its bytes, and the next flush point writes them.
     Caching a handle to skip the walk was considered and rejected - the
     mass-storage export reconnects controllers, so a stored filesystem handle
     can go stale mid-boot. */
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid,
                                    NULL, &Count, &Handles);
  if (EFI_ERROR (Status) || Handles == NULL) {
    if (Handles != NULL) {
      FreePool (Handles);
    }
    return EFI_NOT_FOUND;
  }
  LastStatus = EFI_NOT_FOUND;
  for (Index = 0; Index < Count; Index++) {
    PartEntry = NULL;
    Status = gBS->HandleProtocol (Handles[Index], &gEfiPartitionRecordGuid,
                                  (VOID **)&PartEntry);
    if (EFI_ERROR (Status) || PartEntry == NULL ||
        !SfbGptNameMatchesInline (PartEntry->PartitionName, L"logfs")) {
      continue;
    }
    FileSystem = NULL;
    Status = gBS->HandleProtocol (Handles[Index],
                                  &gEfiSimpleFileSystemProtocolGuid,
                                  (VOID **)&FileSystem);
    if (EFI_ERROR (Status) || FileSystem == NULL ||
        FileSystem->OpenVolume == NULL) {
      LastStatus = EFI_NOT_READY;
      DEBUG ((EFI_D_INFO, "SFB: logfs has no filesystem: %r\n", Status));
      continue;
    }
    Status = FileSystem->OpenVolume (FileSystem, Root);
    if (!EFI_ERROR (Status) && *Root != NULL) {
      FreePool (Handles);
      return EFI_SUCCESS;
    }
    LastStatus = EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
    if (*Root != NULL) {
      (*Root)->Close (*Root);
    }
    *Root = NULL;
  }
  FreePool (Handles);
  return LastStatus;
}

EFI_STATUS
SfbLogFlush (
  IN CONST CHAR8 *Tag
  )
{
  EFI_STATUS         Status;
  EFI_STATUS         CloseStatus;
  EFI_FILE_PROTOCOL *Root;
  EFI_FILE_PROTOCOL *Directory;
  EFI_FILE_PROTOCOL *File;
  CHAR8              Header[256];
  CHAR8             *Captured;
  UINTN              CapturedLength;

  Root = NULL;
  Directory = NULL;
  File = NULL;
  CapturedLength = 0;
  Captured = SfbLogSnapshot (&CapturedLength);
  if (Captured == NULL) {
    CapturedLength = 0;
  }
  Status = SfbOpenLogfsRoot (&Root);
  if (EFI_ERROR (Status) || Root == NULL) {
    /* Through Exit, not straight out: the snapshot above is borrowed and has
       to be released even when there is nowhere to write it. Root, Directory
       and File are all still NULL, so the rest of the cleanup is a no-op.
       This is the common failure - logfs unbound - so leaking the borrow here
       would suspend capture for the rest of the boot. */
    Status = EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;
    goto Exit;
  }
  Status = Root->Open (Root, &Directory, L"\\canoe",
                       EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                       EFI_FILE_MODE_CREATE, EFI_FILE_DIRECTORY);
  if (EFI_ERROR (Status) || Directory == NULL) {
    Status = EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
    goto Exit;
  }
  Status = SfbLogOpenSlot (Directory, &File);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }
  if (AsciiSPrint (Header, sizeof (Header),
                   "Canoe BDS session; tag=%a; captured-bytes=%Lu\r\n",
                   ((Tag == NULL) ? "unspecified" : Tag),
                   (UINT64)CapturedLength) >= sizeof (Header)) {
    Status = EFI_BAD_BUFFER_SIZE;
  }
  if (Status == EFI_SUCCESS) {
    Status = SfbWriteAscii (File, Header);
  }
  if (Status == EFI_SUCCESS) {
    Status = SfbWriteAscii (File, "\r\n[canoe capture; oldest first]\r\n");
  }
  if (Status == EFI_SUCCESS && Captured != NULL) {
    Status = SfbWriteBytes (File, Captured, CapturedLength);
    if (Status == EFI_SUCCESS) {
      Status = SfbWriteAscii (File, "\r\n");
    }
  }
  if (Status == EFI_SUCCESS) {
    Status = SfbWriteAscii (
               File,
               "\r\n--- Canoe BDS session complete ---\r\n");
  }
  if (Status == EFI_SUCCESS) {
    if (File->Flush == NULL) {
      Status = EFI_UNSUPPORTED;
    } else {
      Status = File->Flush (File);
    }
  }

Exit:
  /* Release only what this call borrowed. A NULL snapshot can mean another
     borrow is already outstanding, and releasing then would hand that caller's
     bytes back to the producer underneath it. */
  if (Captured != NULL) {
    SfbLogRelease ();
  }
  if (File != NULL) {
    if (EFI_ERROR (Status)) {
      File->Delete (File);
    } else {
      CloseStatus = File->Close (File);
      if (EFI_ERROR (CloseStatus)) {
        Status = CloseStatus;
      }
    }
  }
  if (Directory != NULL) {
    Directory->Close (Directory);
  }
  if (Root != NULL) {
    Root->Close (Root);
  }
  return Status;
}
