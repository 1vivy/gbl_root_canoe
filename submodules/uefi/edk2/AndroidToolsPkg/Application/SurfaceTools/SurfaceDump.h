/** @file
 *  Bounded SurfaceTools passive and policy dumps for the logfs partition.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __SURFACE_DUMP_H__
#define __SURFACE_DUMP_H__

#include <AndroidToolsUi.h>

#define ST_DUMP_PATH         L"\\SurfaceTools.log"
#define ST_POLICY_DUMP_PATH  L"\\SurfacePolicy.log"

EFI_STATUS
StDumpPassiveInventory (VOID);

EFI_STATUS
StDumpPolicyReport (
  IN CONST AT_REPORT *Report
  );

#endif /* __SURFACE_DUMP_H__ */
