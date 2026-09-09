/** @file
 *  Write the UsbTools census and the last host-mode attempt to logfs.
 *
 *  Mirrors the SurfaceTools dump: the child application locates logfs
 *  independently by GPT partition name and owns only its root and
 *  output-file handles; both are flushed/closed before returning.
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

#include "UsbTools.h"
#include "../../../QcomModulePkg/Application/LinuxLoader/SuperFbGptName.h"

#define UT_DUMP_PATH  L"\\UsbToolsDump.txt"

STATIC
EFI_STATUS
UtWriteBytes (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST VOID        *Buffer,
  IN UINTN             BufferSize
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
UtWriteAscii (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST CHAR8       *Text
  )
{
  UINTN Length;

  Length = 0;
  while (Text[Length] != '\0') {
    Length++;
  }
  return UtWriteBytes (File, Text, Length);
}

STATIC
EFI_STATUS
UtWriteUnicodeLine (
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
  return UtWriteBytes (File, Buffer, Index);
}

STATIC
CONST CHAR16 *
UtPartitionName (
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
UtOpenLogfsRoot (OUT EFI_FILE_PROTOCOL **Root)
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
    Name = UtPartitionName (Handles[Index]);
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
UtCreateDumpFile (
  IN  EFI_FILE_PROTOCOL  *Root,
  OUT EFI_FILE_PROTOCOL **File
  )
{
  EFI_STATUS Status;

  *File = NULL;
  Status = Root->Open (Root, File, UT_DUMP_PATH,
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

  return Root->Open (Root, File, UT_DUMP_PATH,
                     EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                     EFI_FILE_MODE_CREATE, 0);
}

STATIC
EFI_STATUS
UtWriteSection (
  IN EFI_FILE_PROTOCOL      *File,
  IN CONST CHAR16           *Header,
  IN OUT AT_REPORT          *Report
  )
{
  EFI_STATUS  Status;
  UINTN       Index;
  CHAR16      Line[AT_ROW_CHARS];

  UnicodeSPrint (Line, sizeof (Line), L"[%s]", Header);
  Status = UtWriteAscii (File, "\r\n");
  if (Status == EFI_SUCCESS) {
    Status = UtWriteUnicodeLine (File, Line);
  }
  for (Index = 0;
       Index < Report->Count && Status == EFI_SUCCESS;
       Index++) {
    Status = UtWriteUnicodeLine (File, Report->Rows[Index].Text);
  }
  if (Status == EFI_SUCCESS && Report->Truncated) {
    Status = UtWriteAscii (File, "<truncated>\r\n");
  }
  return Status;
}

EFI_STATUS
UtDumpToLogfs (VOID)
{
  EFI_FILE_PROTOCOL *Root;
  EFI_FILE_PROTOCOL *File;
  AT_REPORT         Census;
  EFI_STATUS        Status;
  EFI_STATUS        CloseStatus;

  Root = NULL;
  File = NULL;
  Status = UtOpenLogfsRoot (&Root);
  if (Status != EFI_SUCCESS || Root == NULL) {
    return (Status == EFI_SUCCESS) ? EFI_NOT_FOUND : Status;
  }

  Status = UtCreateDumpFile (Root, &File);
  if (Status == EFI_SUCCESS && File != NULL) {
    Status = UtWriteAscii (
               File,
               "UsbTools diagnostics\r\n"
               "format=1; encoding=ASCII; non-ASCII replaced with ?\r\n");
  }

  if (Status == EFI_SUCCESS) {
    Status = UtBuildCensusReport (&Census);
  }
  if (Status == EFI_SUCCESS) {
    Status = UtWriteSection (File, L"USB Census", &Census);
    AtReportFree (&Census);
  }

  if (Status == EFI_SUCCESS) {
    if (mUtAttemptRan && mUtAttemptReport.Rows != NULL) {
      Status = UtWriteSection (File, L"USB Host Mode Attempt",
                               &mUtAttemptReport);
    } else {
      Status = UtWriteAscii (File, "\r\n[USB Host Mode Attempt]\r\n"
                                   "not run this session\r\n");
    }
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
