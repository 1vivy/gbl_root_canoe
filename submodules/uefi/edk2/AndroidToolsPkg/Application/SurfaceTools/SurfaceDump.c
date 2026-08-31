/** @file
 *  Write bounded SurfaceTools passive and policy reports to logfs.
 *
 *  The Canoe BDS mounts logfs before entering its interactive menus. This
 *  child application locates that filesystem independently and owns only its
 *  root and output-file handles; both are flushed/closed before returning.
 *
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

#include "SurfaceDump.h"
#include "SurfaceInventory.h"
#include "../../../QcomModulePkg/Application/LinuxLoader/SuperFbGptName.h"

extern EFI_GUID gEfiPartitionRecordGuid;


STATIC
EFI_STATUS
StWriteBytes (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST VOID        *Buffer,
  IN UINTN              BufferSize
  )
{
  EFI_STATUS Status;
  UINTN      Written;

  Written = BufferSize;
  Status = File->Write (File, &Written, (VOID *)Buffer);
  if (Status != EFI_SUCCESS) {
    return Status;
  }
  return (Written == BufferSize) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

STATIC
EFI_STATUS
StWriteAscii (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST CHAR8       *Text
  )
{
  UINTN Length;

  Length = 0;
  while (Text[Length] != '\0') {
    Length++;
  }
  return StWriteBytes (File, Text, Length);
}

STATIC
EFI_STATUS
StWriteUnicodeLine (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST CHAR16      *Text
  )
{
  CHAR8 Buffer[AT_ROW_CHARS + 2];
  UINTN Index;

  if (Text == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  for (Index = 0; Index < AT_ROW_CHARS && Text[Index] != L'\0'; Index++) {
    Buffer[Index] = (Text[Index] <= 0x7f) ? (CHAR8)Text[Index] : '?';
  }
  if (Index == AT_ROW_CHARS) {
    return EFI_BAD_BUFFER_SIZE;
  }
  Buffer[Index++] = '\r';
  Buffer[Index++] = '\n';
  return StWriteBytes (File, Buffer, Index);
}

STATIC
EFI_STATUS
StValidatePolicyRow (
  IN CONST CHAR16 *Text
  )
{
  UINTN   Index;
  BOOLEAN HasEquals;

  if (Text == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  HasEquals = FALSE;
  for (Index = 0; Index < AT_ROW_CHARS && Text[Index] != L'\0'; Index++) {
    if (Text[Index] > 0x7f || Text[Index] == L'\r' || Text[Index] == L'\n') {
      return EFI_COMPROMISED_DATA;
    }
    if (Text[Index] == L'=') {
      HasEquals = TRUE;
    }
  }
  if (Index == 0 || Index == AT_ROW_CHARS || !HasEquals) {
    return EFI_COMPROMISED_DATA;
  }
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
StWritePolicyRow (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST CHAR16      *Text
  )
{
  CHAR8 Buffer[AT_ROW_CHARS + 2];
  UINTN Index;
  EFI_STATUS Status;

  Status = StValidatePolicyRow (Text);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Index = 0; Text[Index] != L'\0'; Index++) {
    Buffer[Index] = (CHAR8)Text[Index];
  }
  Buffer[Index++] = '\r';
  Buffer[Index++] = '\n';
  return StWriteBytes (File, Buffer, Index);
}

STATIC
EFI_STATUS
StWritePolicyReport (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST AT_REPORT  *Report
  )
{
  EFI_STATUS Status;
  UINTN      Index;

  if (File == NULL || Report == NULL ||
      (Report->Count != 0 && Report->Rows == NULL) ||
      Report->Count > Report->Capacity) {
    return EFI_INVALID_PARAMETER;
  }
  if (Report->Truncated) {
    return EFI_BUFFER_TOO_SMALL;
  }

  for (Index = 0; Index < Report->Count; Index++) {
    Status = StValidatePolicyRow (Report->Rows[Index].Text);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Status = StWriteAscii (
             File,
             "SurfaceTools policy probe\r\n"
             "format=1\r\n"
             "encoding=ASCII\r\n"
             "policy_payload=complete_hex\r\n"
             "physical_effectiveness=not_observed\r\n"
             "[Policy.v1]\r\n");
  for (Index = 0; Index < Report->Count && Status == EFI_SUCCESS; Index++) {
    Status = StWritePolicyRow (File, Report->Rows[Index].Text);
  }
  return Status;
}

STATIC
CONST CHAR16 *
StPartitionName (
  IN EFI_HANDLE Handle
  )
{
  EFI_PARTITION_ENTRY         *Record;
  EFI_PARTITION_INFO_PROTOCOL *Info;

  Record = NULL;
  if (!EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiPartitionRecordGuid,
                                       (VOID **)&Record)) &&
      Record != NULL) {
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
StOpenLogfsRoot (OUT EFI_FILE_PROTOCOL **Root)
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
    Name = StPartitionName (Handles[Index]);
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
    if (*Root != NULL && (*Root)->Close != NULL) {
      (VOID)(*Root)->Close (*Root);
    }
    *Root = NULL;
  }

  FreePool (Handles);
  return Status;
}

typedef enum {
  StDumpKindPassive,
  StDumpKindPolicy
} ST_DUMP_KIND;

STATIC
EFI_STATUS
StCreateDumpFile (
  IN  EFI_FILE_PROTOCOL *Root,
  OUT EFI_FILE_PROTOCOL **File,
  IN  ST_DUMP_KIND       Kind
  )
{
  EFI_STATUS       Status;
  CHAR16          *Path;

  if (Root == NULL || File == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Path = (Kind == StDumpKindPolicy) ? ST_POLICY_DUMP_PATH : ST_DUMP_PATH;
  *File = NULL;
  Status = Root->Open (Root, File, Path,
                       EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
  if (!EFI_ERROR (Status)) {
    if (*File == NULL) {
      return EFI_DEVICE_ERROR;
    }
    if ((*File)->Delete == NULL) {
      if ((*File)->Close != NULL) {
        (VOID)(*File)->Close (*File);
      }
      *File = NULL;
      return EFI_UNSUPPORTED;
    }
    Status = (*File)->Delete (*File);
    *File = NULL;
    if (Status != EFI_SUCCESS) {
      return Status;
    }
  } else if (Status != EFI_NOT_FOUND) {
    return Status;
  }

  return Root->Open (Root, File, Path,
                     EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                     EFI_FILE_MODE_CREATE, 0);
}

STATIC
EFI_STATUS
StWriteSection (
  IN EFI_FILE_PROTOCOL       *File,
  IN CONST AT_REPORT_SOURCE  *Source
  )
{
  AT_REPORT Report;
  EFI_STATUS Status;
  UINTN Index;
  CHAR16 Header[AT_ROW_CHARS];

  ZeroMem (&Report, sizeof (Report));
  UnicodeSPrint (Header, sizeof (Header), L"[%s]", Source->Title);
  Status = StWriteAscii (File, "\r\n");
  if (Status == EFI_SUCCESS) {
    Status = StWriteUnicodeLine (File, Header);
  }
  if (Status == EFI_SUCCESS) {
    Status = Source->Builder (&Report);
  }
  if (Status != EFI_SUCCESS) {
    AtReportFree (&Report);
    return Status;
  }

  for (Index = 0; Index < Report.Count && Status == EFI_SUCCESS; Index++) {
    Status = StWriteUnicodeLine (File, Report.Rows[Index].Text);
  }
  if (Status == EFI_SUCCESS && Report.Truncated) {
    Status = StWriteAscii (File, "<truncated>\r\n");
  }
  AtReportFree (&Report);
  return Status;
}

STATIC
VOID
StDiscardDumpFile (
  IN OUT EFI_FILE_PROTOCOL **File
  )
{
  if (File == NULL || *File == NULL) {
    return;
  }

  /*
   * EFI_FILE_PROTOCOL.Delete closes the handle whether deletion succeeds or
   * fails. Never call Close on that same handle afterwards.
   */
  if ((*File)->Delete != NULL) {
    (VOID)(*File)->Delete (*File);
    *File = NULL;
    return;
  }
  if ((*File)->Close != NULL) {
    (VOID)(*File)->Close (*File);
  }
  *File = NULL;
}

STATIC
EFI_STATUS
StCloseDumpFile (
  IN OUT EFI_FILE_PROTOCOL **File
  )
{
  EFI_STATUS Status;

  if (File == NULL || *File == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if ((*File)->Close == NULL) {
    *File = NULL;
    return EFI_UNSUPPORTED;
  }

  /* Close consumes the handle even when it reports an error. */
  Status = (*File)->Close (*File);
  *File = NULL;
  return Status;
}

EFI_STATUS
StDumpPassiveInventory (VOID)
{
  EFI_FILE_PROTOCOL *Root;
  EFI_FILE_PROTOCOL *File;
  EFI_STATUS Status;
  EFI_STATUS CloseStatus;
  UINTN Index;

  Root = NULL;
  File = NULL;
  Status = StOpenLogfsRoot (&Root);
  if (Status != EFI_SUCCESS || Root == NULL) {
    return (Status == EFI_SUCCESS) ? EFI_NOT_FOUND : Status;
  }

  Status = StCreateDumpFile (Root, &File, StDumpKindPassive);
  if (Status == EFI_SUCCESS && File == NULL) {
    Status = EFI_DEVICE_ERROR;
  }
  if (Status == EFI_SUCCESS) {
    Status = StWriteAscii (
               File,
               "SurfaceTools passive inventory\r\n"
               "format=1; encoding=ASCII; non-ASCII replaced with ?\r\n");
  }
  for (Index = 0;
       Index < ST_PASSIVE_REPORT_COUNT && Status == EFI_SUCCESS;
       Index++) {
    Status = StWriteSection (File, &gStPassiveReports[Index]);
  }

  if (Status == EFI_SUCCESS) {
    Status = (File->Flush != NULL) ? File->Flush (File) : EFI_UNSUPPORTED;
  }
  if (Status == EFI_SUCCESS) {
    CloseStatus = StCloseDumpFile (&File);
    if (CloseStatus != EFI_SUCCESS) {
      Status = CloseStatus;
    }
  } else {
    StDiscardDumpFile (&File);
  }

  CloseStatus = (Root->Close != NULL) ?
                Root->Close (Root) : EFI_UNSUPPORTED;
  if (Status == EFI_SUCCESS && CloseStatus != EFI_SUCCESS) {
    Status = CloseStatus;
  }
  return Status;
}

EFI_STATUS
StDumpPolicyReport (
  IN CONST AT_REPORT *Report
  )
{
  EFI_FILE_PROTOCOL *Root;
  EFI_FILE_PROTOCOL *File;
  EFI_STATUS Status;
  EFI_STATUS CloseStatus;

  if (Report == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Root = NULL;
  File = NULL;
  Status = StOpenLogfsRoot (&Root);
  if (Status != EFI_SUCCESS || Root == NULL) {
    return (Status == EFI_SUCCESS) ? EFI_NOT_FOUND : Status;
  }

  Status = StCreateDumpFile (Root, &File, StDumpKindPolicy);
  if (Status == EFI_SUCCESS && File == NULL) {
    Status = EFI_DEVICE_ERROR;
  }
  if (Status == EFI_SUCCESS) {
    Status = StWritePolicyReport (File, Report);
  }
  if (Status == EFI_SUCCESS) {
    Status = (File->Flush != NULL) ? File->Flush (File) : EFI_UNSUPPORTED;
  }
  if (Status == EFI_SUCCESS) {
    CloseStatus = StCloseDumpFile (&File);
    if (CloseStatus != EFI_SUCCESS) {
      Status = CloseStatus;
    }
  } else {
    StDiscardDumpFile (&File);
  }

  CloseStatus = (Root->Close != NULL) ?
                Root->Close (Root) : EFI_UNSUPPORTED;
  if (Status == EFI_SUCCESS && CloseStatus != EFI_SUCCESS) {
    Status = CloseStatus;
  }
  return Status;
}
