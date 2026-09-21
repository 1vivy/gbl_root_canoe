/*
 * Select which persistent Canoe log file this session writes.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/SimpleFileSystem.h>

#include "SuperFbLog.h"

#define SFB_LOG_FILE_COUNT   3
/* Enough for the header line's leading fields; seq= is written first. */
#define SFB_LOG_SEQ_PROBE    96

/*
 * Rotation orders slots by a counter carried in the log header, not by the
 * FAT modification time. There is no RTC at this point in boot, so a freshly
 * written file can be stamped older than the stale ones beside it; measured on
 * the OnePlus 15 that pinned eviction to one slot forever, and every session
 * overwrote the previous session while two ancient logs survived untouched.
 * An unreadable or header-less slot sorts as sequence zero, which makes it the
 * first thing evicted - a corrupt slot is the one worth reusing.
 */
STATIC
UINT64
SfbLogSlotSequence (
  IN EFI_FILE_PROTOCOL *File
  )
{
  CHAR8      Probe[SFB_LOG_SEQ_PROBE];
  EFI_STATUS Status;
  UINT64     Sequence;
  UINTN      Size;
  UINTN      Index;

  if (File->Read == NULL) {
    return 0;
  }
  Size = sizeof (Probe);
  Status = File->Read (File, &Size, Probe);
  if (EFI_ERROR (Status) || Size > sizeof (Probe)) {
    return 0;
  }
  for (Index = 0; Index + 4 <= Size; Index++) {
    if (CompareMem (Probe + Index, "seq=", 4) != 0) {
      continue;
    }
    Index += 4;
    Sequence = 0;
    if (Index >= Size || Probe[Index] < '0' || Probe[Index] > '9') {
      return 0;
    }
    for (; Index < Size && Probe[Index] >= '0' && Probe[Index] <= '9'; Index++) {
      if (Sequence > (MAX_UINT64 - 9) / 10) {
        return 0;
      }
      Sequence = Sequence * 10 + (UINT64)(Probe[Index] - '0');
    }
    return Sequence;
  }
  return 0;
}

EFI_STATUS
SfbLogOpenSlot (
  IN  EFI_FILE_PROTOCOL  *Directory,
  OUT EFI_FILE_PROTOCOL **File,
  OUT UINT64             *Sequence
  )
{
  EFI_STATUS         Status;
  EFI_STATUS         CloseStatus;
  EFI_FILE_PROTOCOL *Candidate;
  CHAR16             Name[16];
  UINT64             Highest;
  UINT64             OldestSeq;
  UINT64             SlotSeq;
  UINTN              Index;
  UINTN              LogIndex;
  BOOLEAN            HaveFree;
  BOOLEAN            HaveOldest;

  if (Directory == NULL || File == NULL || Sequence == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *File = NULL;
  *Sequence = 0;
  Highest = 0;
  OldestSeq = 0;
  LogIndex = 0;
  HaveFree = FALSE;
  HaveOldest = FALSE;

  /* Every slot is inspected even once a free one is known: the next sequence
     has to outrank all of them, not just the ones scanned before the gap. */
  for (Index = 0; Index < SFB_LOG_FILE_COUNT; Index++) {
    UnicodeSPrint (Name, sizeof (Name), L"bds-%u.log", (UINT32)Index);
    Candidate = NULL;
    Status = Directory->Open (Directory, &Candidate, Name,
                              EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (Status == EFI_NOT_FOUND) {
      if (Candidate != NULL) {
        Candidate->Close (Candidate);
      }
      if (!HaveFree) {
        LogIndex = Index;
        HaveFree = TRUE;
      }
      continue;
    }
    if (EFI_ERROR (Status) || Candidate == NULL) {
      /* A slot that will not open contributes nothing to the sequence and is
       * not a reason to drop the session: a damaged logfs is exactly when the
       * log matters most. A run with no usable slot at all still fails below. */
      if (Candidate != NULL) {
        Candidate->Close (Candidate);
      }
      continue;
    }

    SlotSeq = SfbLogSlotSequence (Candidate);
    CloseStatus = Candidate->Close (Candidate);
    Candidate = NULL;
    if (EFI_ERROR (CloseStatus)) {
      return CloseStatus;
    }
    if (SlotSeq > Highest) {
      Highest = SlotSeq;
    }
    if (!HaveFree && (!HaveOldest || SlotSeq < OldestSeq)) {
      OldestSeq = SlotSeq;
      LogIndex = Index;
      HaveOldest = TRUE;
    }
  }
  if (!HaveFree && !HaveOldest) {
    return EFI_NOT_FOUND;
  }
  if (Highest == MAX_UINT64) {
    return EFI_OUT_OF_RESOURCES;
  }
  *Sequence = Highest + 1;

  UnicodeSPrint (Name, sizeof (Name), L"bds-%u.log", (UINT32)LogIndex);
  Candidate = NULL;
  Status = Directory->Open (Directory, &Candidate, Name,
                            EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
  if (!EFI_ERROR (Status) && Candidate != NULL) {
    /* Delete/recreate makes a stale tail impossible; SetPosition(0) does not. */
    Status = Candidate->Delete (Candidate);
    Candidate = NULL;
    if (EFI_ERROR (Status)) {
      return Status;
    }
  } else if (Status != EFI_NOT_FOUND) {
    if (Candidate != NULL) {
      Candidate->Close (Candidate);
    }
    return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
  } else if (Candidate != NULL) {
    Candidate->Close (Candidate);
  }

  Status = Directory->Open (Directory, File, Name,
                            EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                            EFI_FILE_MODE_CREATE, 0);
  if (EFI_ERROR (Status) || *File == NULL) {
    if (*File != NULL) {
      (*File)->Close (*File);
      *File = NULL;
    }
    return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}
