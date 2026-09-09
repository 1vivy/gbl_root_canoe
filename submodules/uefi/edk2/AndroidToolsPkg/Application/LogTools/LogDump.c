/** @file
 *  Write the complete LogTools probe report to logfs.
 *  This owns only the logfs root and one report file. Delete-before-create
 *  prevents a shorter run from preserving stale bytes from an older report.
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/SimpleFileSystem.h>
#include "LogTools.h"
#include "../../../QcomModulePkg/Application/LinuxLoader/SuperFbGptName.h"
STATIC
CONST CHAR16 *
LtPartitionName (
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
STATIC
EFI_STATUS
LtOpenLogfsRoot (
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
    Name = LtPartitionName (Handles[Index]);
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
STATIC
EFI_STATUS
LtCreateReportFile (
  IN  EFI_FILE_PROTOCOL  *Root,
  OUT EFI_FILE_PROTOCOL **File
  )
{
  EFI_STATUS Status;

  *File = NULL;
  Status = Root->Open (Root, File, LT_DUMP_PATH,
                       EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
  if (!EFI_ERROR (Status) && *File != NULL) {
    Status = (*File)->Delete (*File);
    *File = NULL;
    if (Status != EFI_SUCCESS) {
      return Status;
    }
  } else if (Status != EFI_NOT_FOUND) {
    return Status;
  }
  return Root->Open (Root, File, LT_DUMP_PATH,
                     EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                     EFI_FILE_MODE_CREATE, 0);
}

EFI_STATUS
LtWriteSection (
  IN EFI_FILE_PROTOCOL       *File,
  IN CONST AT_REPORT_SOURCE  *Source
  )
{
  AT_REPORT Report;
  EFI_STATUS Status;
  UINTN      Index;
  CHAR16     Header[AT_ROW_CHARS];

  ZeroMem (&Report, sizeof (Report));
  UnicodeSPrint (Header, sizeof (Header), L"[%s]", Source->Title);
  Status = LtWriteAscii (File, "\r\n");
  if (Status == EFI_SUCCESS) {
    Status = LtWriteUnicodeLine (File, Header);
  }
  if (Status == EFI_SUCCESS) {
    Status = Source->Builder (&Report);
  }
  if (Status != EFI_SUCCESS) {
    AtReportFree (&Report);
    return Status;
  }
  for (Index = 0; Index < Report.Count && Status == EFI_SUCCESS; Index++) {
    Status = LtWriteUnicodeLine (File, Report.Rows[Index].Text);
  }
  if (Status == EFI_SUCCESS && Report.Truncated) {
    Status = LtWriteAscii (File, "<truncated>\r\n");
  }
  AtReportFree (&Report);
  return Status;
}
EFI_STATUS
LtDumpToLogfs (
  VOID
  )
{
  EFI_FILE_PROTOCOL *Root;
  EFI_FILE_PROTOCOL *File;
  EFI_STATUS         Status;
  EFI_STATUS         CloseStatus;
  UINTN              Index;
  CONST AT_REPORT_SOURCE *Sources;
  UINTN              Count;

  Root = NULL;
  File = NULL;
  /* See LogFat.c: the sweep that makes a filesystem handle appear also emits
     driver output into the live ring the census below reads. */
  LtStartFatStack ();
  Status = LtOpenLogfsRoot (&Root);
  if (Status != EFI_SUCCESS || Root == NULL) {
    return (Status == EFI_SUCCESS) ? EFI_NOT_FOUND : Status;
  }
  Status = LtCreateReportFile (Root, &File);
  if (Status == EFI_SUCCESS && File != NULL) {
    Status = LtWriteAscii (File,
                           "Canoe LogTools diagnostics\r\n"
                           "format=1; encoding=ASCII; read-only probes\r\n"
                           "note=FAT stack connected before these sections;"
                           " live-ring content includes that sweep\r\n");
  }
  Sources = LtReportSources (&Count);
  for (Index = 0; Index < Count && Status == EFI_SUCCESS; Index++) {
    Status = LtWriteSection (File, &Sources[Index]);
  }
  if (Status == EFI_SUCCESS) {
    Status = (File->Flush != NULL) ? File->Flush (File) : EFI_UNSUPPORTED;
  }
  if (Status == EFI_SUCCESS) {
    CloseStatus = File->Close (File);
    File = NULL;
    if (CloseStatus != EFI_SUCCESS) {
      Status = CloseStatus;
    }
  } else if (File != NULL) {
    File->Delete (File);
    File = NULL;
  }
  CloseStatus = Root->Close (Root);
  if (Status == EFI_SUCCESS && CloseStatus != EFI_SUCCESS) {
    Status = CloseStatus;
  }
  return Status;
}
