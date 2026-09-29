/** @file
 *  MdTableLib internals: memory-map snapshots and bounded range gates.
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
  TRUE when a descriptor type is DRAM-like. This is only a secondary range
  gate for addresses obtained from explicit firmware structures; it is never
  permission to probe arbitrary bytes in that descriptor.
**/
BOOLEAN
MdMemoryTypeReadable (
  IN EFI_MEMORY_TYPE Type,
  IN BOOLEAN         IncludeConventional
  );

/**
  Advance to the next DRAM-like descriptor without reading its contents.
  Used by CrashTools to choose an unmapped address. Returns FALSE when
  exhausted or when the descriptor-coverage budget was spent.
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
  Find the descriptor containing Address in an already-taken snapshot.
  Deliberately unfiltered by memory type because callers report the actual
  type and attributes. Does not disturb the walk cursor.
**/
EFI_STATUS
MdMapFindDescriptor (
  IN  CONST MD_MAP_WALK *Walk,
  IN  UINT64            Address,
  OUT MD_MEMORY_INFO    *Info
  );

/**
  TRUE when the whole range [Address, Address + Bytes) lies inside one
  DRAM-like, non-conventional descriptor. This is a secondary range check for
  explicit firmware-provided pointers, never authority for arbitrary reads.
  Fail closed on an empty map, overflow, zero length, or missing descriptor.
**/
BOOLEAN
MdRangeInOneReadableDescriptor (
  IN UINT64 Address,
  IN UINT64 Bytes
  );




/** Plausibility check for a would-be region entry at Address. **/
BOOLEAN
MdEntryPlausible (
  IN EFI_PHYSICAL_ADDRESS Address,
  OUT MD_REGION_ENTRY     *Entry OPTIONAL
  );


#endif /* __MD_TABLE_LIB_INTERNAL_H__ */
