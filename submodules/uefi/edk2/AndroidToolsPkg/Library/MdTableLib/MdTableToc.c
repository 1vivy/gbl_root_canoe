/** @file
 *  Subsystem ToC and G-ToC discovery by back-reference.
 *
 *  Once a region array is found, its owning minidump_subsystem is the struct
 *  whose RegionsBasePtr (offset 24) holds the array base. One scan pass
 *  collects back-references for every discovered array at once; a candidate
 *  must then validate its fourcc fields and region_count before it is
 *  accepted, so an unrelated copy of the same address value cannot be taken
 *  for the ToC.
 *
 *  Subsystem ToCs live inline in the G-ToC's subsystems[] array, so a run of
 *  adjacent valid ToCs gives the G-ToC base (12-byte header before the run)
 *  without ever needing the SMEM base address.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

#include "MdTableLibInternal.h"

typedef struct {
  MD_TABLE_MAP *Map;
} MD_TOC_SCAN;

STATIC
BOOLEAN
MdTocPlausible (
  IN  EFI_PHYSICAL_ADDRESS BasePtrField,
  OUT MD_SUBSYSTEM_TOC     *Toc OPTIONAL
  )
{
  MD_SUBSYSTEM_TOC Candidate;

  if (BasePtrField < MD_SUBSYSTEM_TOC_SIZE - sizeof (UINT64) ||
      BasePtrField - (MD_SUBSYSTEM_TOC_SIZE - sizeof (UINT64)) > MAX_UINTN) {
    return FALSE;
  }
  CopyMem (&Candidate,
           (VOID *)(UINTN)(BasePtrField -
                           (MD_SUBSYSTEM_TOC_SIZE - sizeof (UINT64))),
           sizeof (Candidate));
  if (Candidate.Enabled != MD_SS_ENABLED_VALUE ||
      Candidate.EncryptionStatus != MD_SS_ENCR_DONE_VALUE ||
      Candidate.RegionCount == 0 ||
      Candidate.RegionCount > MD_MAX_ARRAY_ENTRIES) {
    return FALSE;
  }
  if (Toc != NULL) {
    CopyMem (Toc, &Candidate, sizeof (Candidate));
  }
  return TRUE;
}

STATIC
BOOLEAN
MdTocHit (
  IN EFI_PHYSICAL_ADDRESS Address,
  IN UINTN                NeedleIndex,
  IN OUT VOID             *Context
  )
{
  MD_TOC_SCAN     *Scan;
  MD_SUBSYSTEM_TOC Toc;

  Scan = (MD_TOC_SCAN *)Context;
  if (NeedleIndex >= Scan->Map->ArrayCount ||
      Scan->Map->Arrays[NeedleIndex].SubsystemToc != 0) {
    return TRUE;
  }
  if (!MdTocPlausible (Address, &Toc)) {
    return TRUE;
  }
  /* The declared count should equal the walked count exactly; a mismatch is
     still worth recording - it says the array tail is not what we model. */
  Scan->Map->Arrays[NeedleIndex].SubsystemToc =
    Address - (MD_SUBSYSTEM_TOC_SIZE - sizeof (UINT64));
  Scan->Map->Arrays[NeedleIndex].EncryptionRequired = Toc.EncryptionRequired;
  Scan->Map->Arrays[NeedleIndex].TocRegionCount = Toc.RegionCount;
  return TRUE;
}

/** Walk the inline subsystems[] run containing one ToC and read the G-ToC. */
STATIC
VOID
MdFindGtoc (
  IN OUT MD_TABLE_MAP *Map,
  IN     UINTN        ArrayIndex
  )
{
  EFI_PHYSICAL_ADDRESS First;
  EFI_PHYSICAL_ADDRESS Last;
  UINTN                Steps;

  First = Map->Arrays[ArrayIndex].SubsystemToc;
  Last = First;
  for (Steps = 0; Steps < 64; Steps++) {
    if (First < MD_SUBSYSTEM_TOC_SIZE ||
        !MdTocPlausible (First - MD_SUBSYSTEM_TOC_SIZE +
                         (MD_SUBSYSTEM_TOC_SIZE - sizeof (UINT64)), NULL)) {
      break;
    }
    First -= MD_SUBSYSTEM_TOC_SIZE;
  }
  for (Steps = 0; Steps < 64; Steps++) {
    if (!MdTocPlausible (Last + MD_SUBSYSTEM_TOC_SIZE +
                         (MD_SUBSYSTEM_TOC_SIZE - sizeof (UINT64)), NULL)) {
      break;
    }
    Last += MD_SUBSYSTEM_TOC_SIZE;
  }
  Map->SubsystemCount = (UINTN)((Last - First) / MD_SUBSYSTEM_TOC_SIZE) + 1;
  if (First < 12) {
    return;
  }
  Map->GtocAddress = First - 12;
  CopyMem (&Map->Gtoc, (VOID *)(UINTN)Map->GtocAddress, sizeof (Map->Gtoc));
}

VOID
MdTableFindTocs (
  IN OUT MD_TABLE_MAP *Map
  )
{
  UINT64       Needles[MD_MAX_ARRAYS];
  UINT64       Scanned;
  UINTN        Index;
  UINTN        Found;
  MD_TOC_SCAN  Scan;

  if (Map == NULL || Map->ArrayCount == 0) {
    return;
  }
  for (Index = 0; Index < Map->ArrayCount; Index++) {
    Needles[Index] = (UINT64)Map->Arrays[Index].Base;
  }
  Scan.Map = Map;
  MdScanPass (Needles, Map->ArrayCount, MdTocHit, &Scan, FALSE, &Scanned);
  Map->BytesScanned += Scanned;

  /* The G-ToC follows from any array whose ToC was found. */
  for (Found = 0, Index = 0; Index < Map->ArrayCount; Index++) {
    if (Map->Arrays[Index].SubsystemToc != 0) {
      Found++;
    }
  }
  if (Found > 0 && Map->GtocAddress == 0) {
    for (Index = 0; Index < Map->ArrayCount; Index++) {
      if (Map->Arrays[Index].SubsystemToc != 0) {
        MdFindGtoc (Map, Index);
        break;
      }
    }
  }
}
