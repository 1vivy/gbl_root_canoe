/** @file
 *  Write the complete MdTools report to logfs.
 *
 *  Owns only the logfs root and one report file. Delete-before-create keeps
 *  a shorter run from preserving stale bytes of an older report; a short
 *  write reports EFI_DEVICE_ERROR because a truncated report that still
 *  parses invites a wrong conclusion. Report text is ASCII for the host
 *  shell that reads it.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/SimpleFileSystem.h>

#include "MdTools.h"

EFI_STATUS
MdWriteBytes (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST VOID        *Buffer,
  IN UINTN              BufferSize
  )
{
  EFI_STATUS Status;
  UINTN      Written;

  if (File == NULL || File->Write == NULL || Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Written = BufferSize;
  Status = File->Write (File, &Written, (VOID *)Buffer);
  if (Status != EFI_SUCCESS) {
    return Status;
  }
  return (Written == BufferSize) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS
MdWriteAscii (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST CHAR8       *Text
  )
{
  UINTN Length;

  if (Text == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Length = 0;
  while (Text[Length] != '\0') {
    Length++;
  }
  return MdWriteBytes (File, Text, Length);
}

EFI_STATUS
MdWriteUnicodeLine (
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
  /* A row that fills the buffer exactly cannot be terminated, and silently
     dropping its tail would misreport a measurement. */
  if (Index == AT_ROW_CHARS) {
    return EFI_BAD_BUFFER_SIZE;
  }
  Buffer[Index++] = '\r';
  Buffer[Index++] = '\n';
  return MdWriteBytes (File, Buffer, Index);
}

STATIC
EFI_STATUS
MdCreateReportFile (
  IN  EFI_FILE_PROTOCOL  *Root,
  OUT EFI_FILE_PROTOCOL **File
  )
{
  EFI_STATUS Status;

  *File = NULL;
  Status = Root->Open (Root, File, MD_DUMP_PATH,
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
  return Root->Open (Root, File, MD_DUMP_PATH,
                     EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                     EFI_FILE_MODE_CREATE, 0);
}

STATIC
EFI_STATUS
MdWriteSection (
  IN EFI_FILE_PROTOCOL      *File,
  IN CONST AT_REPORT_SOURCE *Source
  )
{
  AT_REPORT  Report;
  EFI_STATUS Status;
  UINTN      Index;
  CHAR16     Header[AT_ROW_CHARS];

  ZeroMem (&Report, sizeof (Report));
  UnicodeSPrint (Header, sizeof (Header), L"[%s]", Source->Title);
  Status = MdWriteAscii (File, "\r\n");
  if (Status == EFI_SUCCESS) {
    Status = MdWriteUnicodeLine (File, Header);
  }
  if (Status == EFI_SUCCESS) {
    Status = Source->Builder (&Report);
  }
  if (Status != EFI_SUCCESS) {
    AtReportFree (&Report);
    return Status;
  }
  for (Index = 0; Index < Report.Count && Status == EFI_SUCCESS; Index++) {
    Status = MdWriteUnicodeLine (File, Report.Rows[Index].Text);
  }
  if (Status == EFI_SUCCESS && Report.Truncated) {
    Status = MdWriteAscii (File, "<truncated>\r\n");
  }
  AtReportFree (&Report);
  return Status;
}

EFI_STATUS
MdDumpToLogfs (
  VOID
  )
{
  EFI_FILE_PROTOCOL      *Root;
  EFI_FILE_PROTOCOL      *File;
  EFI_STATUS             Status;
  EFI_STATUS             CloseStatus;
  UINTN                  Index;
  CONST AT_REPORT_SOURCE *Sources;
  UINTN                  Count;

  Root = NULL;
  File = NULL;
  /* The scan must already have happened for the sections to have content. */
  Status = MdEnsureScan ();
  if (EFI_ERROR (Status)) {
    return Status;
  }
  /* See MdFat.c: the sweep that makes a filesystem handle appear also emits
     driver output into the live UEFI ring. */
  MdStartFatStack ();
  Status = MdOpenLogfsRoot (&Root);
  if (Status != EFI_SUCCESS || Root == NULL) {
    return (Status == EFI_SUCCESS) ? EFI_NOT_FOUND : Status;
  }
  Status = MdCreateReportFile (Root, &File);
  if (Status == EFI_SUCCESS && File != NULL) {
    Status = MdWriteAscii (File,
                           "Canoe MdTools minidump table probe\r\n"
                           "format=1; encoding=ASCII\r\n"
                           "note=FAT stack connected before these sections;"
                           " live UEFI ring content includes that sweep\r\n"
                           "note=table edits are RAM-only; XBL rebuilds the"
                           " table every boot\r\n");
  }
  Sources = MdReportSources (&Count);
  for (Index = 0; Index < Count && Status == EFI_SUCCESS; Index++) {
    Status = MdWriteSection (File, &Sources[Index]);
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
