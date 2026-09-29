/** @file
  Pure parsing helpers for the bounded Qualcomm minidump SMEM root.

  Copyright (c) 2026, contributors to the canoe ABL tree.
  SPDX-License-Identifier: BSD-3-Clause
**/

#include <Uefi.h>

#include <Library/BaseMemoryLib.h>

#include <MdTable.h>

STATIC
BOOLEAN
MdSubsystemTocIsZero (
  IN CONST MD_SUBSYSTEM_TOC *Toc
  )
{
  CONST UINT8 *Bytes;
  UINTN       Index;

  Bytes = (CONST UINT8 *)Toc;
  for (Index = 0; Index < sizeof (*Toc); ++Index) {
    if (Bytes[Index] != 0) {
      return FALSE;
    }
  }

  return TRUE;
}

STATIC
BOOLEAN
MdSubsystemTocIsMapped (
  IN CONST MD_SUBSYSTEM_TOC *Toc
  )
{
  return (BOOLEAN)(
           Toc->Status != 0 &&
           Toc->Enabled == MD_SS_ENABLED_VALUE &&
           Toc->RegionCount > 0 &&
           Toc->RegionCount <= MD_MAX_APPEND_REGIONS &&
           Toc->RegionsBasePtr != 0
           );
}

STATIC
VOID
MdMapSubsystem (
  IN  CONST MD_GLOBAL_TOC *Root,
  IN  UINTN               SubsystemIndex,
  OUT MD_REGION_ARRAY     *Array
  )
{
  CONST MD_SUBSYSTEM_TOC *Toc;

  Toc = &Root->Subsystems[SubsystemIndex];
  ZeroMem (Array, sizeof (*Array));
  Array->Base               = (EFI_PHYSICAL_ADDRESS)Toc->RegionsBasePtr;
  Array->Count              = Toc->RegionCount;
  Array->SubsystemIndex     = SubsystemIndex;
  Array->SubsystemToc       = (EFI_PHYSICAL_ADDRESS)(UINTN)Toc;
  Array->EncryptionRequired = Toc->EncryptionRequired;
  Array->TocRegionCount     = Toc->RegionCount;
  CopyMem (&Array->Toc, Toc, sizeof (Array->Toc));
  Array->TocBasePtr = Toc->RegionsBasePtr;
}

EFI_STATUS
MdTableMapRoot (
  IN  EFI_PHYSICAL_ADDRESS Address,
  IN  UINTN                Bytes,
  OUT MD_TABLE_MAP         *Map
  )
{
  CONST MD_GLOBAL_TOC *Root;
  UINTN               RequiredIndex;
  STATIC CONST UINTN   RequiredSubsystems[] = { MD_SS_AOP, MD_SS_BOOT };

  if (Map == NULL || Address == 0) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Map, sizeof (*Map));
  if (Bytes < sizeof (MD_GLOBAL_TOC)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Root = (CONST MD_GLOBAL_TOC *)(UINTN)Address;
  Map->GtocAddress          = Address;
  Map->GtocBytes            = Bytes;
  Map->GtocFromSmem         = TRUE;
  Map->SubsystemCount       = MD_MAX_SUBSYSTEMS;
  Map->GtocStructContiguous = TRUE;
  CopyMem (&Map->Gtoc, &Root->Header, sizeof (Map->Gtoc));

  if (Map->Gtoc.Status == 0 || Map->Gtoc.Revision != MD_GTOC_REVISION) {
    return EFI_COMPROMISED_DATA;
  }

  Map->GtocHeaderValid = TRUE;
  for (RequiredIndex = 0;
       RequiredIndex < sizeof (RequiredSubsystems) / sizeof (RequiredSubsystems[0]);
       ++RequiredIndex) {
    UINTN SubsystemIndex;

    SubsystemIndex = RequiredSubsystems[RequiredIndex];
    if (!MdSubsystemTocIsMapped (&Root->Subsystems[SubsystemIndex])) {
      continue;
    }

    if (Map->ArrayCount >= MD_MAX_ARRAYS) {
      return EFI_OUT_OF_RESOURCES;
    }

    MdMapSubsystem (
      Root,
      SubsystemIndex,
      &Map->Arrays[Map->ArrayCount]
      );
    ++Map->ArrayCount;
  }

  return (Map->ArrayCount == 0) ? EFI_NOT_FOUND : EFI_SUCCESS;
}

EFI_STATUS
MdTableSelectFreeSubsystem (
  IN  CONST MD_SUBSYSTEM_TOC *Slots,
  IN  UINTN                  SlotCount,
  OUT UINTN                  *Index,
  OUT BOOLEAN                *AboveHighest,
  OUT BOOLEAN                *AnyUsed,
  OUT UINTN                  *HighestUsed
  )
{
  BOOLEAN FoundUsed;
  UINTN   FirstFree;
  UINTN   Highest;
  UINTN   Slot;

  if (Slots == NULL || SlotCount == 0 || Index == NULL ||
      AboveHighest == NULL || AnyUsed == NULL || HighestUsed == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  FoundUsed = FALSE;
  FirstFree = SlotCount;
  Highest   = 0;

  for (Slot = 0; Slot < SlotCount; ++Slot) {
    if (MdSubsystemTocIsZero (&Slots[Slot])) {
      if (FirstFree == SlotCount) {
        FirstFree = Slot;
      }
      continue;
    }

    FoundUsed = TRUE;
    Highest   = Slot;
  }

  if (FoundUsed) {
    for (Slot = Highest + 1; Slot < SlotCount; ++Slot) {
      if (MdSubsystemTocIsZero (&Slots[Slot])) {
        *Index        = Slot;
        *AboveHighest = TRUE;
        *AnyUsed      = TRUE;
        *HighestUsed  = Highest;
        return EFI_SUCCESS;
      }
    }
  }

  if (FirstFree == SlotCount) {
    return EFI_NOT_FOUND;
  }

  *Index        = FirstFree;
  *AboveHighest = (BOOLEAN)!FoundUsed;
  *AnyUsed      = FoundUsed;
  *HighestUsed  = Highest;
  return EFI_SUCCESS;
}
