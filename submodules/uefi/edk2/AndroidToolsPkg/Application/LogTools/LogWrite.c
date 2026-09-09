/** @file
 *  File-writing primitives for the LogTools report.
 *
 *  A short write is treated as a failure rather than a partial success: a
 *  truncated diagnostic report that still looks well-formed is worse than no
 *  report, because it invites a conclusion drawn from missing rows.
 *
 *  Report text is ASCII on purpose. The reader for these files is a shell on a
 *  host, and the platform's own log beside them is ASCII too; a UTF-16 file
 *  would be the only thing in the directory needing special handling.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Protocol/SimpleFileSystem.h>

#include "LogTools.h"

EFI_STATUS
LtWriteBytes (
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
LtWriteAscii (
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
  return LtWriteBytes (File, Text, Length);
}

EFI_STATUS
LtWriteUnicodeLine (
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
  return LtWriteBytes (File, Buffer, Index);
}
