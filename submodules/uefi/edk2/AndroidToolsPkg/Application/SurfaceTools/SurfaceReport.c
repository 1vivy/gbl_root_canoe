/** @file
 *  Bounded report storage and formatting for SurfaceTools.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>

#include "SurfaceInventory.h"

CONST ST_REPORT_SOURCE gStPassiveReports[ST_PASSIVE_REPORT_COUNT] = {
  { L"Execution Summary",      StBuildSummaryReport },
  { L"Known Policy Surfaces", StBuildPolicyReport },
  { L"Protocol GUID Census",  StBuildProtocolReport },
  { L"Configuration Tables",  StBuildTableReport },
  { L"Loaded Images",         StBuildImageReport },
  { L"Memory Map",            StBuildMemoryReport },
};

EFI_STATUS
StReportInit (
  OUT ST_REPORT *Report,
  IN  UINTN      Capacity
  )
{
  if (Report == NULL || Capacity == 0 ||
      Capacity > MAX_UINTN / sizeof (ST_ROW)) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Report, sizeof (*Report));
  Report->Rows = AllocateZeroPool (Capacity * sizeof (ST_ROW));
  if (Report->Rows == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  Report->Capacity = Capacity;
  return EFI_SUCCESS;
}

VOID
StReportFree (
  IN OUT ST_REPORT *Report
  )
{
  if (Report == NULL) {
    return;
  }
  if (Report->Rows != NULL) {
    FreePool (Report->Rows);
  }
  ZeroMem (Report, sizeof (*Report));
}

CHAR16 *
StReportNextRow (
  IN OUT ST_REPORT *Report
  )
{
  if (Report == NULL || Report->Rows == NULL) {
    return NULL;
  }
  if (Report->Count >= Report->Capacity) {
    Report->Truncated = TRUE;
    return NULL;
  }

  return Report->Rows[Report->Count++].Text;
}

VOID
StFormatGuid (
  IN  CONST EFI_GUID *Guid,
  OUT CHAR16         *Buffer,
  IN  UINTN           BufferChars
  )
{
  if (Buffer == NULL || BufferChars == 0) {
    return;
  }
  Buffer[0] = L'\0';
  if (Guid == NULL || BufferChars < 37) {
    return;
  }

  UnicodeSPrint (
      Buffer, BufferChars * sizeof (CHAR16),
      L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      Guid->Data1, Guid->Data2, Guid->Data3,
      Guid->Data4[0], Guid->Data4[1], Guid->Data4[2], Guid->Data4[3],
      Guid->Data4[4], Guid->Data4[5], Guid->Data4[6], Guid->Data4[7]);
}
