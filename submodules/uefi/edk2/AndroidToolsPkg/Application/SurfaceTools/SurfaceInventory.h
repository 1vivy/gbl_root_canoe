/** @file
 *  Read-only UEFI surface collectors used by SurfaceTools.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __SURFACE_INVENTORY_H__
#define __SURFACE_INVENTORY_H__

#include <Uefi.h>
#include <AndroidToolsUi.h>
#include "SurfaceModel.h"

/* The bounded report model (AT_REPORT, AtReportAdd, the paged viewer) lives
 * in the AndroidToolsUi library, shared with the other tools. */
#define ST_PASSIVE_REPORT_COUNT  6u

extern CONST AT_REPORT_SOURCE gStPassiveReports[ST_PASSIVE_REPORT_COUNT];

VOID
StFormatGuid (
  IN  CONST EFI_GUID *Guid,
  OUT CHAR16         *Buffer,
  IN  UINTN           BufferChars
  );

EFI_STATUS StBuildSummaryReport (OUT AT_REPORT *Report);
EFI_STATUS StBuildPolicyReport (OUT AT_REPORT *Report);
EFI_STATUS StBuildProtocolReport (OUT AT_REPORT *Report);
EFI_STATUS StBuildTableReport (OUT AT_REPORT *Report);
EFI_STATUS StBuildImageReport (OUT AT_REPORT *Report);
EFI_STATUS StBuildMemoryReport (OUT AT_REPORT *Report);
EFI_STATUS StBuildProbeReport (OUT AT_REPORT *Report);

#endif /* __SURFACE_INVENTORY_H__ */
