/** @file
 *  Bind the FAT stack and locate logfs for the MdTools report.
 *
 *  Same constraint as LogTools: launched straight from the ABL, nothing has
 *  connected the log partition to a filesystem driver yet, so one
 *  unconditional connect sweep runs before the volume is looked up by GPT
 *  partition name. The sweep's own driver output lands in the live UEFI ring;
 *  the report header says so, and the table scan is unaffected by it.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/SimpleFileSystem.h>

#include "MdTools.h"
#include "../../../QcomModulePkg/Application/LinuxLoader/SuperFbGptName.h"

VOID
MdStartFatStack (
  VOID
  )
{
  STATIC BOOLEAN Started = FALSE;
  EFI_HANDLE    *Handles;
  UINTN          Count;
  UINTN          Index;
  EFI_STATUS     Status;

  /* Repeating the pass cannot bind anything new and costs a full connect
     sweep, so the first attempt is the only one. */
  if (Started) {
    return;
  }
  Started = TRUE;

  Handles = NULL;
  Count = 0;
  Status = gBS->LocateHandleBuffer (AllHandles, NULL, NULL, &Count, &Handles);
  if (EFI_ERROR (Status) || Handles == NULL) {
    return;
  }
  for (Index = 0; Index < Count; Index++) {
    gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
  }
  FreePool (Handles);
}

STATIC
CONST CHAR16 *
MdPartitionName (
  IN EFI_HANDLE Handle
  )
{
  EFI_PARTITION_ENTRY         *Record;
  EFI_PARTITION_INFO_PROTOCOL *Info;

  Record = NULL;
  if (!EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiPartitionRecordGuid,
                                       (VOID **)&Record)) && Record != NULL) {
    return Record->PartitionName;
  }
  Info = NULL;
  if (!EFI_ERROR (gBS->HandleProtocol (Handle,
                                       &gEfiPartitionInfoProtocolGuid,
                                       (VOID **)&Info)) &&
      Info != NULL && Info->Type == PARTITION_TYPE_GPT) {
    return Info->Info.Gpt.PartitionName;
  }
  return NULL;
}

EFI_STATUS
MdOpenLogfsRoot (
  OUT EFI_FILE_PROTOCOL **Root
  )
{
  EFI_STATUS                       Status;
  EFI_HANDLE                      *Handles;
  UINTN                            Count;
  UINTN                            Index;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  CONST CHAR16                    *Name;

  if (Root == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Root = NULL;
  Handles = NULL;
  Count = 0;
  Status = gBS->LocateHandleBuffer (ByProtocol,
                                    &gEfiSimpleFileSystemProtocolGuid,
                                    NULL, &Count, &Handles);
  if (EFI_ERROR (Status) || Handles == NULL) {
    return EFI_NOT_FOUND;
  }
  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < Count; Index++) {
    Name = MdPartitionName (Handles[Index]);
    if (!SfbGptNameMatchesInline (Name, L"logfs")) {
      continue;
    }
    FileSystem = NULL;
    Status = gBS->HandleProtocol (Handles[Index],
                                  &gEfiSimpleFileSystemProtocolGuid,
                                  (VOID **)&FileSystem);
    if (EFI_ERROR (Status) || FileSystem == NULL ||
        FileSystem->OpenVolume == NULL) {
      Status = EFI_NOT_FOUND;
      continue;
    }
    Status = FileSystem->OpenVolume (FileSystem, Root);
    if (!EFI_ERROR (Status) && *Root != NULL) {
      break;
    }
    *Root = NULL;
  }
  FreePool (Handles);
  return Status;
}
