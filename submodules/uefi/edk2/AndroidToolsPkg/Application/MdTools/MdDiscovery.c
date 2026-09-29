/** @file
  Session-cached bounded discovery with durable stage markers.

  Copyright (c) 2026, contributors to the canoe ABL tree.
  SPDX-License-Identifier: BSD-3-Clause
**/

#include <Uefi.h>

#include "MdTools.h"

STATIC MD_TABLE_MAP mMdMap;
STATIC BOOLEAN      mMdScanned = FALSE;
STATIC EFI_STATUS   mMdScanStatus = EFI_NOT_READY;

EFI_STATUS
MdEnsureScan (
  IN OUT MD_EVIDENCE *Evidence
  )
{
  EFI_STATUS           Status;
  EFI_PHYSICAL_ADDRESS Address;
  UINTN                Bytes;

  if (Evidence == NULL || !Evidence->Open) {
    return EFI_INVALID_PARAMETER;
  }
  if (mMdScanned) {
    Status = MdEvidencePrint (
               Evidence,
               L"stage: discovery cached status=%r arrays=%u",
               mMdScanStatus,
               (UINT32)mMdMap.ArrayCount
               );
    if (EFI_ERROR (Status)) {
      return Status;
    }
    Status = MdEvidenceFlush (Evidence);
    return EFI_ERROR (Status) ? Status : mMdScanStatus;
  }

  Status = MdEvidencePrint (
             Evidence,
             L"stage: smem-locate pending item=%u",
             MD_SMEM_ITEM_ID
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = MdEvidenceFlush (Evidence);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mMdScanned = TRUE;
  Address    = 0;
  Bytes      = 0;
  Status = MdTableLocateRoot (&Address, &Bytes);
  mMdScanStatus = Status;
  Status = MdEvidencePrint (
             Evidence,
             L"stage: smem-locate status=%r root=0x%lx bytes=%u",
             mMdScanStatus,
             (UINT64)Address,
             (UINT32)Bytes
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = MdEvidenceFlush (Evidence);
  if (EFI_ERROR (Status) || EFI_ERROR (mMdScanStatus)) {
    return EFI_ERROR (Status) ? Status : mMdScanStatus;
  }

  Status = MdEvidencePrint (
             Evidence,
             L"stage: bounded-map pending root=0x%lx bytes=%u",
             (UINT64)Address,
             (UINT32)Bytes
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = MdEvidenceFlush (Evidence);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mMdScanStatus = MdTableScanRoot (Address, Bytes, &mMdMap);
  Status = MdEvidencePrint (
             Evidence,
             L"stage: bounded-map status=%r arrays=%u",
             mMdScanStatus,
             (UINT32)mMdMap.ArrayCount
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = MdEvidenceFlush (Evidence);
  return EFI_ERROR (Status) ? Status : mMdScanStatus;
}

CONST MD_TABLE_MAP *
MdCachedMap (
  VOID
  )
{
  return mMdScanned ? &mMdMap : NULL;
}
