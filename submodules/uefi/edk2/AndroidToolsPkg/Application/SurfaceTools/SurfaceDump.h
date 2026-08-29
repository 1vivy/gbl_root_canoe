/** @file
 *  Bounded passive-inventory dump for the logfs partition.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __SURFACE_DUMP_H__
#define __SURFACE_DUMP_H__

#include <Uefi.h>

#define ST_DUMP_PATH  L"\\SurfaceTools.log"

EFI_STATUS
StDumpPassiveInventory (VOID);

#endif /* __SURFACE_DUMP_H__ */
