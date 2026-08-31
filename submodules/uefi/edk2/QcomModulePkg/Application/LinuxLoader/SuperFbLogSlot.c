/*
 * Select which persistent Canoe log file this session writes.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <Uefi.h>
#include <Guid/FileInfo.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/SimpleFileSystem.h>

#define SFB_LOG_FILE_COUNT   3

typedef struct {
  EFI_FILE_INFO Info;
  CHAR16       ExtraName[9];
} SFB_FILE_INFO;

STATIC
UINT64
SfbTimeKey (
  IN CONST EFI_TIME *Time
  )
{
  return (((((((UINT64)Time->Year * 13 + Time->Month) * 32 + Time->Day) * 24 +
             Time->Hour) * 60 + Time->Minute) * 60) + Time->Second);
}

EFI_STATUS
SfbLogOpenSlot (
  IN  EFI_FILE_PROTOCOL  *Directory,
  OUT EFI_FILE_PROTOCOL **File
  )
{
  EFI_STATUS         Status;
  EFI_STATUS         CloseStatus;
  EFI_FILE_PROTOCOL *Candidate;
  SFB_FILE_INFO      Info;
  CHAR16             Name[16];
  UINT64             OldestKey;
  UINTN              InfoSize;
  UINTN              Index;
  UINTN              LogIndex;
  BOOLEAN            HaveOldest;

  if (Directory == NULL || File == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *File = NULL;
  HaveOldest = FALSE;

  for (Index = 0; Index < SFB_LOG_FILE_COUNT; Index++) {
    UnicodeSPrint (Name, sizeof (Name), L"bds-%u.log", (UINT32)Index);
    Candidate = NULL;
    Status = Directory->Open (Directory, &Candidate, Name,
                              EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (Status == EFI_NOT_FOUND) {
      if (Candidate != NULL) {
        Candidate->Close (Candidate);
      }
      LogIndex = Index;
      break;
    }
    if (EFI_ERROR (Status) || Candidate == NULL) {
      if (Candidate != NULL) {
        Candidate->Close (Candidate);
      }
      return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
    }

    InfoSize = sizeof (Info);
    Status = Candidate->GetInfo (Candidate, &gEfiFileInfoGuid,
                                &InfoSize, &Info);
    CloseStatus = Candidate->Close (Candidate);
    Candidate = NULL;
    if (EFI_ERROR (Status)) {
      return Status;
    }
    if (EFI_ERROR (CloseStatus)) {
      return CloseStatus;
    }
    if (!HaveOldest ||
        SfbTimeKey (&Info.Info.ModificationTime) < OldestKey) {
      OldestKey = SfbTimeKey (&Info.Info.ModificationTime);
      LogIndex = Index;
      HaveOldest = TRUE;
    }
  }
  if (Index == SFB_LOG_FILE_COUNT && !HaveOldest) {
    return EFI_NOT_FOUND;
  }

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
