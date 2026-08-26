/*
 * Reading the active slot out of the GPT.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbSlots.h"
#include "SuperFbGptName.h"

#include <Library/DebugLib.h>
#include <Library/PartitionTableUpdate.h>

/* Keeps the translation unit legal when the feature is compiled out. */
CONST CHAR8 *gSfbSlotsModuleTag = "SuperFbSlots";

STATIC SFB_SLOT  mActiveSlot = SfbSlotUnknown;
STATIC BOOLEAN   mActiveSlotKnown = FALSE;

STATIC
CONST CHAR8 *
SfbSlotText (IN SFB_SLOT Slot)
{
  switch (Slot) {
  case SfbSlotA:
    return "a";
  case SfbSlotB:
    return "b";
  case SfbSlotUnknown:
  default:
    return "unknown";
  }
}

SFB_SLOT
SfbActiveSlot (VOID)
{
  UINT32   Count = 0;
  UINT32   Index;
  BOOLEAN  FoundA = FALSE;
  BOOLEAN  FoundB = FALSE;
  BOOLEAN  ActiveA = FALSE;
  BOOLEAN  ActiveB = FALSE;

  if (mActiveSlotKnown) {
    return mActiveSlot;
  }

  /*
   * LinuxLoader enumerates the partition table and updates these entries before
   * the menu is built, so PtnEntries is populated by the time anything here
   * asks. abl is the partition this boot chain actually depends on, which makes
   * it the right pair to read.
   */
  GetPartitionCount (&Count);
  if (Count > MAX_NUM_PARTITIONS) {
    Count = MAX_NUM_PARTITIONS;
  }

  for (Index = 0; Index < Count; Index++) {
    CONST EFI_PARTITION_ENTRY  *Entry = &PtnEntries[Index].PartEntry;
    BOOLEAN                     Active;

    Active = (BOOLEAN)((Entry->Attributes & PART_ATT_ACTIVE_VAL) != 0);

    /* PartitionName is a CHAR16[36] with no guaranteed terminator, space
     * padded by some writers, so StrCmp against it is wrong. */
    if (SfbGptNameMatchesInline (Entry->PartitionName, L"abl_a")) {
      FoundA = TRUE;
      ActiveA = Active;
    } else if (SfbGptNameMatchesInline (Entry->PartitionName, L"abl_b")) {
      FoundB = TRUE;
      ActiveB = Active;
    }
  }

  /*
   * Both names must be present and exactly one of them marked. A single-slot
   * device, a layout that names its loader differently, and a table that marks
   * neither or both all answer Unknown: there is nothing to verify against, and
   * an unrecognised layout must never be allowed to suppress a boot.
   */
  if (FoundA && FoundB && ActiveA != ActiveB) {
    mActiveSlot = ActiveA ? SfbSlotA : SfbSlotB;
  } else {
    mActiveSlot = SfbSlotUnknown;
  }
  mActiveSlotKnown = TRUE;

  DEBUG ((EFI_D_INFO, "SFB: MARK active-slot slot=%a\n",
          SfbSlotText (mActiveSlot)));

  return mActiveSlot;
}
