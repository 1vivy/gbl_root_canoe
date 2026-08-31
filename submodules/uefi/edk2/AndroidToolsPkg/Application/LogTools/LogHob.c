/** @file
 *  Locate a GUID HOB whose data area holds a pointer.
 *
 *  Two things this tool asks about are published exactly this way - the
 *  Qualcomm info block and the shared-library loader - so the walk lives here
 *  once rather than in each collector. Every read goes through the memory-map
 *  validated copy, because a HOB chain is firmware-supplied and a corrupt
 *  length would otherwise walk this loop off into nothing.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Guid/HobList.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Pi/PiBootMode.h>
#include <Pi/PiHob.h>

#include "LogTools.h"

EFI_STATUS
LtReadGuidHobPointer (
  IN  CONST EFI_GUID *Name,
  OUT VOID          **Pointer
  )
{
  EFI_HOB_GENERIC_HEADER Generic;
  EFI_HOB_GUID_TYPE      GuidHob;
  EFI_STATUS             Status;
  EFI_PHYSICAL_ADDRESS   HobAddress;
  UINTN                  HobList;
  UINTN                  TableIndex;
  UINTN                  HobIndex;
  UINTN                  PointerValue;

  if (Name == NULL || Pointer == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Pointer = NULL;
  if (gST == NULL || gST->ConfigurationTable == NULL) {
    return EFI_NOT_FOUND;
  }

  HobList = 0;
  for (TableIndex = 0; TableIndex < gST->NumberOfTableEntries; TableIndex++) {
    if (CompareGuid (&gST->ConfigurationTable[TableIndex].VendorGuid,
                     &gEfiHobListGuid)) {
      HobList = (UINTN)gST->ConfigurationTable[TableIndex].VendorTable;
      break;
    }
  }
  if (HobList == 0) {
    return EFI_NOT_FOUND;
  }

  HobAddress = (EFI_PHYSICAL_ADDRESS)HobList;
  for (HobIndex = 0; HobIndex < LT_MAX_HOBS; HobIndex++) {
    Status = LtCopyPhysical (HobAddress, &Generic, sizeof (Generic));
    if (EFI_ERROR (Status) || Generic.HobLength < sizeof (Generic) ||
        Generic.HobLength > LT_MAX_PHYSICAL_READ ||
        HobAddress > MAX_UINT64 - Generic.HobLength) {
      return EFI_COMPROMISED_DATA;
    }
    if (Generic.HobType == EFI_HOB_TYPE_END_OF_HOB_LIST) {
      return EFI_NOT_FOUND;
    }
    if (Generic.HobType != EFI_HOB_TYPE_GUID_EXTENSION) {
      HobAddress += Generic.HobLength;
      continue;
    }
    if (Generic.HobLength < sizeof (GuidHob) ||
        EFI_ERROR (LtCopyPhysical (HobAddress, &GuidHob, sizeof (GuidHob)))) {
      return EFI_COMPROMISED_DATA;
    }
    if (!CompareGuid (&GuidHob.Name, Name)) {
      HobAddress += Generic.HobLength;
      continue;
    }
    /* Found the HOB. From here a failure is about the pointer it carries, and
       the caller needs that distinction to report why nothing was read. */
    if (Generic.HobLength < sizeof (GuidHob) + sizeof (PointerValue)) {
      return EFI_COMPROMISED_DATA;
    }
    Status = LtCopyPhysical (HobAddress + sizeof (GuidHob),
                             &PointerValue, sizeof (PointerValue));
    if (EFI_ERROR (Status) || PointerValue == 0) {
      return EFI_ACCESS_DENIED;
    }
    *Pointer = (VOID *)PointerValue;
    return EFI_SUCCESS;
  }
  return EFI_COMPROMISED_DATA;
}
