/** @file
 *  MdTableLib internals: memory-map walk and the shared DDR scan driver.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __MD_TABLE_LIB_INTERNAL_H__
#define __MD_TABLE_LIB_INTERNAL_H__

#include <Uefi.h>
#include <MdTable.h>

/* Total bytes any single scan pass may cover before it stops and reports
   truncation; keeps a pathological memory map from pinning the tool. */
#define MD_SCAN_BUDGET_BYTES  (6ULL * 1024ULL * 1024ULL * 1024ULL)

typedef struct {
  EFI_MEMORY_DESCRIPTOR *Map;
  UINTN                 MapSize;
  UINTN                 DescriptorSize;
  UINTN                 Index;
  UINT64                Scanned;
  BOOLEAN               IncludeConventional;
} MD_MAP_WALK;

/** Snapshot the UEFI memory map. Free with MdMapWalkFree. **/
EFI_STATUS
MdMapWalkInit (
  IN OUT MD_MAP_WALK *Walk,
  IN     BOOLEAN     IncludeConventional
  );

/**
  Advance to the next descriptor the probe may safely read (no MMIO, and no
  conventional memory unless requested). Returns FALSE when exhausted or when
  the scan budget was spent (check Walk->Scanned against MD_SCAN_BUDGET_BYTES).
**/
BOOLEAN
MdMapWalkNext (
  IN OUT MD_MAP_WALK *Walk,
  OUT    UINT64      *Base,
  OUT    UINT64      *Size
  );

VOID
MdMapWalkFree (
  IN OUT MD_MAP_WALK *Walk
  );

/**
  Called for every 8-aligned UINT64 in a readable range that equals one of the
  needles. Return TRUE to keep scanning, FALSE to stop the pass early.
**/
typedef BOOLEAN (*MD_SCAN_HIT_FN) (
  IN EFI_PHYSICAL_ADDRESS Address,
  IN UINTN                NeedleIndex,
  IN OUT VOID             *Context
  );

/**
  Drive one scan pass over the readable map. OnHit fires on needle matches.
  *BytesScanned accumulates the coverage. Returns EFI_SUCCESS normally,
  EFI_NOT_FOUND if no hit, EFI_BUFFER_TOO_SMALL if the budget ran out.
**/
EFI_STATUS
MdScanPass (
  IN     CONST UINT64 *Needles,
  IN     UINTN        NeedleCount,
  IN     MD_SCAN_HIT_FN OnHit,
  IN OUT VOID         *Context,
  IN     BOOLEAN      IncludeConventional,
  OUT    UINT64       *BytesScanned
  );

/** Plausibility check for a would-be region entry at Address. **/
BOOLEAN
MdEntryPlausible (
  IN EFI_PHYSICAL_ADDRESS Address,
  OUT MD_REGION_ENTRY     *Entry OPTIONAL
  );

/** Subsystem ToC discovery pass (MdTableToc.c); called by MdTableScan. **/
VOID
MdTableFindTocs (
  IN OUT MD_TABLE_MAP *Map
  );

#endif /* __MD_TABLE_LIB_INTERNAL_H__ */
