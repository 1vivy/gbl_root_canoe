/** @file
 *  Anchor scan and region-array reconstruction for the minidump table.
 *
 *  Anchors are the first eight bytes of region names measured on the target
 *  (2026-09-02 Sahara capture, 66 regions): XBL_LOG/UEFI_LOG in the boot
 *  subsystem, TZ_DDR in secure world, CPUCP_MISC_D in the co-processor
 *  subsystem, SMEMINFO in the shared-memory group. A name-prefix hit must
 *  still validate as a full plausible entry before it anchors an array, so a
 *  coincidental byte pattern cannot fabricate a table.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include "MdTableLibInternal.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)  (sizeof (a) / sizeof ((a)[0]))
#endif

typedef struct {
  CONST CHAR8 *Name;    /* full expected entry name                    */
  UINT64      FirstWord;/* first 8 name bytes, little-endian           */
} MD_ANCHOR;

STATIC CONST MD_ANCHOR mMdAnchors[] = {
  { "XBL_LOG",      0x00474F4C5F4C4258ULL },
  { "UEFI_LOG",     0x474F4C5F49464555ULL },
  { "TZ_DDR",       0x00005244445F5A54ULL },
  { "CPUCP_MISC_D", 0x494D5F5043555043ULL },
  { "SMEMINFO",     0x4F464E494D454D53ULL },
};

STATIC
BOOLEAN
MdNameMatches (
  IN CONST CHAR8 *EntryName,
  IN CONST CHAR8 *Expected
  )
{
  UINTN Length;
  UINTN Index;

  Length = AsciiStrLen (Expected);
  if (Length == 0 || Length > MD_REGION_NAME_LEN) {
    return FALSE;
  }
  for (Index = 0; Index < Length; Index++) {
    if (EntryName[Index] != Expected[Index]) {
      return FALSE;
    }
  }
  /* A shorter expected name must be followed by NUL padding in the entry. */
  return (BOOLEAN)(Length == MD_REGION_NAME_LEN || EntryName[Length] == '\0');
}

/** Extend [First..Last] over contiguous plausible entries around Anchor. */
STATIC
VOID
MdWalkArray (
  IN  EFI_PHYSICAL_ADDRESS Anchor,
  OUT EFI_PHYSICAL_ADDRESS *Base,
  OUT UINTN                *Count
  )
{
  EFI_PHYSICAL_ADDRESS First;
  EFI_PHYSICAL_ADDRESS Last;
  UINTN                Steps;

  First = Anchor;
  for (Steps = 0; Steps < MD_MAX_ARRAY_ENTRIES; Steps++) {
    if (First < MD_REGION_ENTRY_SIZE ||
        !MdEntryPlausible (First - MD_REGION_ENTRY_SIZE, NULL)) {
      break;
    }
    First -= MD_REGION_ENTRY_SIZE;
  }
  Last = Anchor;
  for (Steps = 0; Steps < MD_MAX_ARRAY_ENTRIES; Steps++) {
    if (!MdEntryPlausible (Last + MD_REGION_ENTRY_SIZE, NULL)) {
      break;
    }
    Last += MD_REGION_ENTRY_SIZE;
  }
  *Base = First;
  *Count = (UINTN)((Last - First) / MD_REGION_ENTRY_SIZE) + 1;
}

STATIC
BOOLEAN
MdAnchorHit (
  IN EFI_PHYSICAL_ADDRESS Address,
  IN UINTN                NeedleIndex,
  IN OUT VOID             *Context
  )
{
  MD_TABLE_MAP         *Map;
  MD_REGION_ENTRY      Entry;
  EFI_PHYSICAL_ADDRESS Base;
  UINTN                Count;
  UINTN                Index;
  UINT64               End;
  UINT64               KnownEnd;

  Map = (MD_TABLE_MAP *)Context;
  if (NeedleIndex >= ARRAY_SIZE (mMdAnchors) ||
      !MdEntryPlausible (Address, &Entry) ||
      !MdNameMatches (Entry.Name, mMdAnchors[NeedleIndex].Name)) {
    return TRUE;
  }
  Map->AnchorHits++;
  Map->AnchorMask |= (1u << NeedleIndex);
  MdWalkArray (Address, &Base, &Count);
  End = (UINT64)Base + (UINT64)Count * MD_REGION_ENTRY_SIZE;
  /* A second anchor inside an array already recorded only confirms it. */
  for (Index = 0; Index < Map->ArrayCount; Index++) {
    KnownEnd = (UINT64)Map->Arrays[Index].Base +
               (UINT64)Map->Arrays[Index].Count * MD_REGION_ENTRY_SIZE;
    if ((UINT64)Base < KnownEnd && End > (UINT64)Map->Arrays[Index].Base) {
      Map->Arrays[Index].AnchorCount++;
      return TRUE;
    }
  }
  if (Map->ArrayCount >= MD_MAX_ARRAYS) {
    return FALSE;
  }
  ZeroMem (&Map->Arrays[Map->ArrayCount], sizeof (MD_REGION_ARRAY));
  Map->Arrays[Map->ArrayCount].Base = Base;
  Map->Arrays[Map->ArrayCount].Count = Count;
  Map->Arrays[Map->ArrayCount].AnchorCount = 1;
  Map->ArrayCount++;
  return TRUE;
}

EFI_STATUS
MdTableScan (
  OUT MD_TABLE_MAP *Map
  )
{
  UINT64    Needles[sizeof (mMdAnchors) / sizeof (mMdAnchors[0])];
  UINT64    Scanned;
  UINTN     Index;
  EFI_STATUS Status;
  EFI_TIME  Start;
  EFI_TIME  End;

  if (Map == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (Map, sizeof (*Map));
  for (Index = 0; Index < ARRAY_SIZE (mMdAnchors); Index++) {
    Needles[Index] = mMdAnchors[Index].FirstWord;
  }
  Map->AnchorTotal = ARRAY_SIZE (mMdAnchors);
  gRT->GetTime (&Start, NULL);
  Status = MdScanPass (Needles, ARRAY_SIZE (Needles), MdAnchorHit, Map,
                       FALSE, &Scanned);
  Map->BytesScanned = Scanned;
  Map->ScanTruncated = (BOOLEAN)(Status == EFI_BUFFER_TOO_SMALL);
  if (Map->AnchorHits == 0) {
    /* Nothing in reserved/pool memory: retry including conventional RAM. */
    Status = MdScanPass (Needles, ARRAY_SIZE (Needles), MdAnchorHit, Map,
                         TRUE, &Scanned);
    Map->BytesScanned += Scanned;
    Map->ScanTruncated = (BOOLEAN)(Map->ScanTruncated ||
                                   Status == EFI_BUFFER_TOO_SMALL);
  }
  gRT->GetTime (&End, NULL);
  Map->ScanSeconds = (End.Second >= Start.Second)
                     ? (UINT32)(End.Second - Start.Second)
                     : (UINT32)(End.Second + 60 - Start.Second);

  for (Index = 0; Index < Map->ArrayCount; Index++) {
    MD_REGION_ENTRY Entry;
    if (!EFI_ERROR (MdTableReadEntry (Map, Index, 0, &Entry))) {
      CopyMem (Map->Arrays[Index].FirstName, Entry.Name, MD_REGION_NAME_LEN);
    }
    if (!EFI_ERROR (MdTableReadEntry (Map, Index,
                                      Map->Arrays[Index].Count - 1, &Entry))) {
      CopyMem (Map->Arrays[Index].LastName, Entry.Name, MD_REGION_NAME_LEN);
    }
  }
  MdTableFindTocs (Map);

  return (Map->ArrayCount > 0) ? EFI_SUCCESS : EFI_NOT_FOUND;
}

EFI_STATUS
MdTableReadEntry (
  IN  CONST MD_TABLE_MAP *Map,
  IN  UINTN              ArrayIndex,
  IN  UINTN              EntryIndex,
  OUT MD_REGION_ENTRY    *Entry
  )
{
  EFI_PHYSICAL_ADDRESS Where;

  if (Map == NULL || Entry == NULL || ArrayIndex >= Map->ArrayCount ||
      EntryIndex >= Map->Arrays[ArrayIndex].Count) {
    return EFI_INVALID_PARAMETER;
  }
  Where = Map->Arrays[ArrayIndex].Base +
          (EFI_PHYSICAL_ADDRESS)EntryIndex * MD_REGION_ENTRY_SIZE;
  if (!MdEntryPlausible (Where, Entry)) {
    return EFI_COMPROMISED_DATA;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
MdTableFindInArray (
  IN  CONST MD_TABLE_MAP *Map,
  IN  UINTN              ArrayIndex,
  IN  CONST CHAR8        *Name,
  OUT UINTN              *EntryIndex,
  OUT MD_REGION_ENTRY    *Entry OPTIONAL
  )
{
  MD_REGION_ENTRY Current;
  UINTN           Index;
  EFI_STATUS      Status;

  if (Map == NULL || Name == NULL || EntryIndex == NULL ||
      ArrayIndex >= Map->ArrayCount) {
    return EFI_INVALID_PARAMETER;
  }
  for (Index = 0; Index < Map->Arrays[ArrayIndex].Count; Index++) {
    Status = MdTableReadEntry (Map, ArrayIndex, Index, &Current);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    if (MdNameMatches (Current.Name, Name)) {
      *EntryIndex = Index;
      if (Entry != NULL) {
        CopyMem (Entry, &Current, sizeof (Current));
      }
      return EFI_SUCCESS;
    }
  }
  return EFI_NOT_FOUND;
}

EFI_STATUS
MdTableFindArray (
  IN  CONST MD_TABLE_MAP *Map,
  IN  CONST CHAR8        *Name,
  OUT UINTN              *ArrayIndex
  )
{
  UINTN Index;
  UINTN EntryIndex;

  if (Map == NULL || Name == NULL || ArrayIndex == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  for (Index = 0; Index < Map->ArrayCount; Index++) {
    if (!EFI_ERROR (MdTableFindInArray (Map, Index, Name, &EntryIndex, NULL))) {
      *ArrayIndex = Index;
      return EFI_SUCCESS;
    }
  }
  return EFI_NOT_FOUND;
}
