/** @file
 *  Census the UART ring published through the live info block.
 *
 *  Marker offsets are measured in the published byte array, not guessed from
 *  a write-head variable which this application cannot safely access.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

#include "LogTools.h"

STATIC
BOOLEAN
LtRingPrintable (
  IN UINT8 Byte
  )
{
  return (BOOLEAN)(Byte >= 0x20 && Byte <= 0x7e);
}

STATIC
UINTN
LtMarkerLength (
  IN CONST CHAR8 *Marker
  )
{
  UINTN Length;

  Length = 0;
  while (Marker[Length] != '\0') {
    Length++;
  }
  return Length;
}

STATIC
VOID
LtFindMarker (
  IN  CONST UINT8  *Bytes,
  IN  UINTN          Length,
  IN  CONST CHAR8   *Marker,
  OUT BOOLEAN       *Found,
  OUT UINTN         *First,
  OUT UINTN         *Last
  )
{
  UINTN Span;
  UINTN Index;

  *Found = FALSE;
  *First = 0;
  *Last = 0;
  Span = LtMarkerLength (Marker);
  if (Span == 0 || Length < Span) {
    return;
  }
  for (Index = 0; Index <= Length - Span; Index++) {
    if (CompareMem (Bytes + Index, Marker, Span) == 0) {
      if (!*Found) {
        *First = Index;
        *Found = TRUE;
      }
      *Last = Index;
    }
  }
}

STATIC
UINT64
LtRingPercentTenth (
  IN UINTN Part,
  IN UINTN Total
  )
{
  if (Total == 0) {
    return 0;
  }
  return ((UINT64)Part * 1000 + Total / 2) / Total;
}

EFI_STATUS
LtBuildRingReport (
  OUT AT_REPORT *Report
  )
{
  LT_INFO_BLOCK Info;
  EFI_STATUS    Status;
  UINT8        *Bytes;
  UINTN         Length;
  UINTN         Used;
  UINTN         Index;
  UINTN         Printable;
  UINTN         Tenth;
  BOOLEAN       Found;
  UINTN         First;
  UINTN         Last;

  if (Report == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Status = AtReportInit (Report, LT_REPORT_ROWS);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  (VOID)LtReadInfoBlock (&Info);
  if (!Info.Readable) {
    AtReportAdd (Report, L"ring-readable=no (info block status=%r)",
                 Info.Status);
    return EFI_SUCCESS;
  }
  AtReportAdd (Report, L"ring-address=0x%016lx length=0x%016lx",
               Info.UartLogBufferPtr, Info.UartLogBufferLen);
  if (Info.UartLogBufferPtr == 0 || Info.UartLogBufferLen == 0 ||
      Info.UartLogBufferLen > LT_MAX_RING_LENGTH ||
      Info.UartLogBufferLen > MAX_UINTN) {
    AtReportAdd (Report, L"ring-readable=no (length/address outside bound)");
    return EFI_SUCCESS;
  }
  Length = (UINTN)Info.UartLogBufferLen;
  Bytes = AllocatePool (Length);
  if (Bytes == NULL) {
    AtReportAdd (Report, L"ring-readable=no (allocation failed)");
    return EFI_SUCCESS;
  }
  Status = LtCopyPhysical ((EFI_PHYSICAL_ADDRESS)Info.UartLogBufferPtr,
                           Bytes, Length);
  if (EFI_ERROR (Status)) {
    AtReportAdd (Report, L"ring-readable=no (copy status=%r)", Status);
    FreePool (Bytes);
    return EFI_SUCCESS;
  }
  Used = Length;
  while (Used != 0 && Bytes[Used - 1] == 0) {
    Used--;
  }
  Printable = 0;
  for (Index = 0; Index < Used; Index++) {
    if (LtRingPrintable (Bytes[Index])) {
      Printable++;
    }
  }
  Tenth = LtRingPercentTenth (Printable, Used);
  AtReportAdd (Report, L"ring-readable=yes");
  AtReportAdd (Report, L"used-extent=0x%lx (%Lu bytes)",
               (UINT64)Used, (UINT64)Used);
  AtReportAdd (Report, L"full=%s", (Used == Length) ? L"yes" : L"no");
  AtReportAdd (Report, L"printable-ratio=%Lu/%Lu (%Lu.%01Lu%%)",
               (UINT64)Printable, (UINT64)Used, Tenth / 10, Tenth % 10);

  LtFindMarker (Bytes, Used, "SFB: MARK", &Found, &First, &Last);
  if (Found) {
    AtReportAdd (Report, L"SFB: MARK first-offset=0x%lx last-offset=0x%lx",
                 (UINT64)First, (UINT64)Last);
  } else {
    AtReportAdd (Report, L"SFB: MARK first-offset=none last-offset=none");
  }
  LtFindMarker (Bytes, Used, "[PHOENIX2.0]", &Found, &First, &Last);
  if (Found) {
    AtReportAdd (Report,
                 L"[PHOENIX2.0] first-offset=0x%lx last-offset=0x%lx",
                 (UINT64)First, (UINT64)Last);
  } else {
    AtReportAdd (Report,
                 L"[PHOENIX2.0] first-offset=none last-offset=none");
  }
  FreePool (Bytes);
  return EFI_SUCCESS;
}
