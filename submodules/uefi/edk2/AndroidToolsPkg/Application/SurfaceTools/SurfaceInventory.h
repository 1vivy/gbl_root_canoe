/** @file
 *  Read-only UEFI surface collectors used by SurfaceTools.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __SURFACE_INVENTORY_H__
#define __SURFACE_INVENTORY_H__

#include <Uefi.h>
#include <Library/PrintLib.h>
#include "SurfaceModel.h"

#define ST_ROW_CHARS  96u

typedef struct {
  CHAR16 Text[ST_ROW_CHARS];
} ST_ROW;

typedef struct {
  ST_ROW  *Rows;
  UINTN    Count;
  UINTN    Capacity;
  BOOLEAN  Truncated;
} ST_REPORT;

#define ST_PASSIVE_REPORT_COUNT  6u

typedef EFI_STATUS (*ST_REPORT_BUILDER)(OUT ST_REPORT *Report);

typedef struct {
  CONST CHAR16      *Title;
  ST_REPORT_BUILDER  Builder;
} ST_REPORT_SOURCE;

extern CONST ST_REPORT_SOURCE gStPassiveReports[ST_PASSIVE_REPORT_COUNT];

EFI_STATUS
StReportInit (
  OUT ST_REPORT *Report,
  IN  UINTN      Capacity
  );

VOID
StReportFree (
  IN OUT ST_REPORT *Report
  );

CHAR16 *
StReportNextRow (
  IN OUT ST_REPORT *Report
  );

#define StReportAdd(Report, Format, ...) do {                              \
  ST_REPORT *StReport__ = (Report);                                        \
  CHAR16 *StReportRow__ = StReportNextRow (StReport__);                    \
  if (StReportRow__ != NULL) {                                             \
    UINTN StReportLength__ = UnicodeSPrint (                               \
      StReportRow__, ST_ROW_CHARS * sizeof (CHAR16),                       \
      (Format), ##__VA_ARGS__);                                            \
    if (StReportLength__ >= ST_ROW_CHARS - 1) {                            \
      StReport__->Truncated = TRUE;                                        \
    }                                                                      \
  }                                                                        \
} while (FALSE)

VOID
StFormatGuid (
  IN  CONST EFI_GUID *Guid,
  OUT CHAR16         *Buffer,
  IN  UINTN           BufferChars
  );

EFI_STATUS StBuildSummaryReport (OUT ST_REPORT *Report);
EFI_STATUS StBuildPolicyReport (OUT ST_REPORT *Report);
EFI_STATUS StBuildProtocolReport (OUT ST_REPORT *Report);
EFI_STATUS StBuildTableReport (OUT ST_REPORT *Report);
EFI_STATUS StBuildImageReport (OUT ST_REPORT *Report);
EFI_STATUS StBuildMemoryReport (OUT ST_REPORT *Report);
EFI_STATUS StBuildProbeReport (OUT ST_REPORT *Report);

#endif /* __SURFACE_INVENTORY_H__ */
