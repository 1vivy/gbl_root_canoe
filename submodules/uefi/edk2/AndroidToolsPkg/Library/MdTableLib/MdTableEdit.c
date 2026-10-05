/** @file
  Owned-subsystem edit for the live Qualcomm minidump table.

  The only supported mutation stores one 32-byte subsystem ToC into a free slot
  of the SMEM-provided global root. The slot is selected from the full bounded
  root, cloned from the live AOP not-encryption-required template, range-gated,
  cache-cleaned, and read back. XBL rebuilds the table on every boot.

  Copyright (c) 2026, contributors to the canoe ABL tree.
  SPDX-License-Identifier: BSD-3-Clause
**/

#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>

#include "MdTableLibInternal.h"

EFI_STATUS
MdTablePlanSubsystemClaim (
  IN  CONST MD_TABLE_MAP *Map,
  OUT MD_SUBSYSTEM_CLAIM *Claim
  )
{
  CONST MD_GLOBAL_TOC *Root;
  MD_SUBSYSTEM_TOC    Toc;
  EFI_STATUS          Status;
  UINTN               Index;
  UINTN               Chosen;
  UINTN               Highest;
  BOOLEAN             AboveHighest;
  BOOLEAN             AnyUsed;

  if (Map == NULL || Claim == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (!Map->GtocFromSmem ||
      Map->GtocAddress == 0 ||
      Map->GtocBytes < sizeof (MD_GLOBAL_TOC) ||
      !Map->GtocHeaderValid ||
      !Map->GtocDescribed ||
      !Map->GtocStructContiguous) {
    return EFI_NOT_FOUND;
  }
  if (!MdMemoryTypeReadable (Map->GtocMemory.Type, FALSE)) {
    return EFI_NOT_FOUND;
  }

  ZeroMem (Claim, sizeof (*Claim));
  Claim->TemplateArray = MD_MAX_ARRAYS;
  for (Index = 0; Index < Map->ArrayCount; ++Index) {
    if (Map->Arrays[Index].SubsystemIndex != MD_SS_AOP ||
        Map->Arrays[Index].SubsystemToc == 0 ||
        Map->Arrays[Index].EncryptionRequired != MD_SS_ENCR_NOTREQ_VALUE) {
      continue;
    }

    CopyMem (&Toc, &Map->Arrays[Index].Toc, sizeof (Toc));
    if (Toc.Status != MD_SS_AOP_TOC_MAGIC_VALUE ||
        Toc.Enabled != MD_SS_ENABLED_VALUE ||
        Toc.EncryptionStatus != MD_SS_ENCR_DONE_VALUE ||
        Toc.EncryptionRequired != MD_SS_ENCR_NOTREQ_VALUE ||
        Toc.Pad != 0) {
      return EFI_COMPROMISED_DATA;
    }
    CopyMem (&Claim->Template, &Toc, sizeof (Toc));
    Claim->TemplateArray = Index;
    Claim->TemplateToc   = Map->Arrays[Index].SubsystemToc;
    break;
  }
  if (Claim->TemplateToc == 0) {
    return EFI_NOT_FOUND;
  }

  Root = (CONST MD_GLOBAL_TOC *)(UINTN)Map->GtocAddress;
  Status = MdTableSelectFreeSubsystem (
             Root->Subsystems,
             MD_MAX_SUBSYSTEMS,
             &Chosen,
             &AboveHighest,
             &AnyUsed,
             &Highest
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Claim->Index        = Chosen;
  Claim->TocAddress   = (EFI_PHYSICAL_ADDRESS)(UINTN)&Root->Subsystems[Chosen];
  Claim->AboveHighest = AboveHighest;
  Claim->AnyUsed      = AnyUsed;
  Claim->HighestUsed  = Highest;

  if (!MdRangeWritable ((UINT64)Claim->TocAddress, sizeof (Claim->Previous))) {
    return EFI_ACCESS_DENIED;
  }
  CopyMem (
    &Claim->Previous,
    (VOID *)(UINTN)Claim->TocAddress,
    sizeof (Claim->Previous)
    );
  return EFI_SUCCESS;
}

EFI_STATUS
MdTableClaimSubsystem (
  IN OUT MD_TABLE_MAP        *Map,
  IN     CONST MD_SUBSYSTEM_CLAIM *Claim,
  IN     UINT64              RegionsBasePtr,
  IN     UINT32              RegionCount,
  OUT    MD_SUBSYSTEM_TOC    *Stored OPTIONAL
  )
{
  MD_SUBSYSTEM_TOC     Current;
  MD_SUBSYSTEM_TOC     Toc;
  MD_SUBSYSTEM_TOC     ReadBack;
  EFI_PHYSICAL_ADDRESS ExpectedSlot;
  UINT64               Words[MD_SUBSYSTEM_TOC_SIZE / sizeof (UINT64)];

  if (Map == NULL || Claim == NULL ||
      !Map->GtocFromSmem ||
      Claim->Index >= MD_MAX_SUBSYSTEMS || Claim->TocAddress == 0 ||
      Claim->TemplateToc == 0 || Claim->Template.Status == 0 ||
      RegionsBasePtr == 0 || RegionsBasePtr > MAX_UINTN ||
      RegionCount == 0 || RegionCount > MD_MAX_APPEND_REGIONS) {
    return EFI_INVALID_PARAMETER;
  }

  ExpectedSlot = Map->GtocAddress + MD_GTOC_HEADER_SIZE +
                 Claim->Index * MD_SUBSYSTEM_TOC_SIZE;
  if (Claim->TocAddress != ExpectedSlot ||
      !MdRangeWritable ((UINT64)Claim->TocAddress, sizeof (Current))) {
    return EFI_ACCESS_DENIED;
  }

  /* Recheck the slot immediately before the store. The intent record captured
     an all-zero slot; any intervening change aborts rather than overwriting it. */
  CopyMem (&Current, (VOID *)(UINTN)Claim->TocAddress, sizeof (Current));
  if (CompareMem (&Current, &Claim->Previous, sizeof (Current)) != 0) {
    return EFI_ABORTED;
  }
  CopyMem (Words, &Current, sizeof (Words));
  if ((Words[0] | Words[1] | Words[2] | Words[3]) != 0) {
    return EFI_ALREADY_STARTED;
  }

  CopyMem (&Toc, &Claim->Template, sizeof (Toc));
  Toc.RegionCount   = RegionCount;
  Toc.RegionsBasePtr = RegionsBasePtr;

  CopyMem ((VOID *)(UINTN)Claim->TocAddress, &Toc, sizeof (Toc));
  WriteBackInvalidateDataCacheRange (
    (VOID *)(UINTN)Claim->TocAddress,
    sizeof (Toc)
    );
  CopyMem (&ReadBack, (VOID *)(UINTN)Claim->TocAddress, sizeof (ReadBack));
  if (Stored != NULL) {
    CopyMem (Stored, &ReadBack, sizeof (ReadBack));
  }
  return (CompareMem (&Toc, &ReadBack, sizeof (Toc)) == 0)
         ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS
MdTableReleaseSubsystemClaim (
  IN OUT MD_TABLE_MAP             *Map,
  IN     CONST MD_SUBSYSTEM_CLAIM *Claim,
  IN     CONST MD_SUBSYSTEM_TOC   *Expected
  )
{
  MD_SUBSYSTEM_TOC     Current;
  MD_SUBSYSTEM_TOC     ReadBack;
  EFI_PHYSICAL_ADDRESS ExpectedSlot;

  if (Map == NULL || Claim == NULL || Expected == NULL ||
      !Map->GtocFromSmem || Claim->Index >= MD_MAX_SUBSYSTEMS ||
      Claim->TocAddress == 0) {
    return EFI_INVALID_PARAMETER;
  }
  ExpectedSlot = Map->GtocAddress + MD_GTOC_HEADER_SIZE +
                 Claim->Index * MD_SUBSYSTEM_TOC_SIZE;
  if (Claim->TocAddress != ExpectedSlot ||
      !MdRangeWritable ((UINT64)Claim->TocAddress, sizeof (Current))) {
    return EFI_ACCESS_DENIED;
  }
  CopyMem (&Current, (VOID *)(UINTN)Claim->TocAddress, sizeof (Current));
  if (CompareMem (&Current, Expected, sizeof (Current)) != 0) {
    return EFI_ABORTED;
  }
  CopyMem ((VOID *)(UINTN)Claim->TocAddress, &Claim->Previous,
           sizeof (Claim->Previous));
  WriteBackInvalidateDataCacheRange (
    (VOID *)(UINTN)Claim->TocAddress,
    sizeof (Claim->Previous)
    );
  CopyMem (&ReadBack, (VOID *)(UINTN)Claim->TocAddress, sizeof (ReadBack));
  return (CompareMem (&Claim->Previous, &ReadBack, sizeof (ReadBack)) == 0)
         ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}
