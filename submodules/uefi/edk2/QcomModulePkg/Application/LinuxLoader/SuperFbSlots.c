/*
 * Reading the active slot out of the GPT.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbSlots.h"
#include "SuperFbGptName.h"
#include "SuperFbSlotAttributes.h"

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

STATIC
VOID
SfbReadRetryState (
  IN  CONST EFI_PARTITION_ENTRY  *Entry,
  OUT SFB_SLOT_RETRY_STATE        *State
  )
{
  State->Available = TRUE;
  State->RetryCount = (SFB_UINT8)SfbSlotRetryGet (Entry->Attributes);
  State->Successful = (SFB_BOOLEAN)SfbSlotSuccessfulGet (Entry->Attributes);
  State->Unbootable = (SFB_BOOLEAN)SfbSlotUnbootableGet (Entry->Attributes);
}

SFB_SLOT_RETRIES
SfbSlotRetries (VOID)
{
  SFB_SLOT_RETRIES Retries = { 0 };
  UINT32           Count = 0;
  UINT32           Index;

  GetPartitionCount (&Count);
  if (Count > MAX_NUM_PARTITIONS) {
    Count = MAX_NUM_PARTITIONS;
  }

  for (Index = 0; Index < Count; Index++) {
    CONST EFI_PARTITION_ENTRY *Entry = &PtnEntries[Index].PartEntry;

    if (SfbGptNameMatchesInline (Entry->PartitionName, L"abl_a")) {
      SfbReadRetryState (Entry, &Retries.A);
    } else if (SfbGptNameMatchesInline (Entry->PartitionName, L"abl_b")) {
      SfbReadRetryState (Entry, &Retries.B);
    }
  }

  return Retries;
}

STATIC
SFB_SLOT
SfbReadActiveSlotFromTable (VOID)
{
  UINT32   Count = 0;
  UINT32   Index;
  BOOLEAN  FoundA = FALSE;
  BOOLEAN  FoundB = FALSE;
  BOOLEAN  ActiveA = FALSE;
  BOOLEAN  ActiveB = FALSE;

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

  if (FoundA && FoundB && ActiveA != ActiveB) {
    return ActiveA ? SfbSlotA : SfbSlotB;
  }
  return SfbSlotUnknown;
}

SFB_SLOT
SfbActiveSlot (VOID)
{
  if (mActiveSlotKnown) {
    return mActiveSlot;
  }

  /*
   * LinuxLoader enumerates the partition table and updates these entries before
   * the menu is built, so PtnEntries is populated by the time anything here
   * asks. abl is the partition this boot chain actually depends on, which makes
   * it the right pair to read.
   */
  mActiveSlot = SfbReadActiveSlotFromTable ();
  mActiveSlotKnown = TRUE;

  DEBUG ((EFI_D_INFO, "SFB: MARK active-slot slot=%a\n",
          SfbSlotText (mActiveSlot)));

  return mActiveSlot;
}

EFI_STATUS
SfbResetActiveSlotRetry (VOID)
{
  SFB_SLOT             ActiveSlot;
  EFI_PARTITION_ENTRY *ActiveEntry = NULL;
  UINT32               Count = 0;
  UINT32               Index;

  /* The write decision always uses a fresh table read, never the display cache. */
  ActiveSlot = SfbReadActiveSlotFromTable ();
  if (ActiveSlot == SfbSlotUnknown) {
    return EFI_NOT_FOUND;
  }

  GetPartitionCount (&Count);
  if (Count > MAX_NUM_PARTITIONS) {
    Count = MAX_NUM_PARTITIONS;
  }
  for (Index = 0; Index < Count; Index++) {
    EFI_PARTITION_ENTRY *Entry = &PtnEntries[Index].PartEntry;
    BOOLEAN Matches = (BOOLEAN)(
        (ActiveSlot == SfbSlotA &&
         SfbGptNameMatchesInline (Entry->PartitionName, L"abl_a")) ||
        (ActiveSlot == SfbSlotB &&
         SfbGptNameMatchesInline (Entry->PartitionName, L"abl_b")));

    if (Matches) {
      if (ActiveEntry != NULL ||
          (Entry->Attributes & PART_ATT_ACTIVE_VAL) == 0) {
        return EFI_COMPROMISED_DATA;
      }
      ActiveEntry = Entry;
    }
  }

  if (ActiveEntry == NULL) {
    return EFI_NOT_FOUND;
  }
  if (!SfbSlotResetRetryAttributes (&ActiveEntry->Attributes)) {
    return EFI_COMPROMISED_DATA;
  }

  return UpdatePartitionAttributes (PARTITION_ATTRIBUTES);
}
