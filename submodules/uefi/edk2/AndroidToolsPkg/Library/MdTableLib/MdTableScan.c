/** @file
  Bounded SMEM discovery and validated reads for the Qualcomm minidump table.

  Copyright (c) 2026, contributors to the canoe ABL tree.
  SPDX-License-Identifier: BSD-3-Clause
**/

#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "MdTableLibInternal.h"

#define QCOM_SMEM_PROTOCOL_REVISION 0x0000000000010001ULL

typedef
EFI_STATUS
(EFIAPI *QCOM_SMEM_GET_ADDR)(
  IN  UINT32 Item,
  OUT UINT32 *Size,
  OUT VOID   **Address
  );

typedef struct {
  UINT64             Revision;
  VOID               *SmemAlloc;
  QCOM_SMEM_GET_ADDR SmemGetAddr;
  VOID               *SmemAllocEx;
  VOID               *SmemGetAddrEx;
} QCOM_SMEM_PROTOCOL;

STATIC EFI_GUID mQcomSmemProtocolGuid = {
  0xf4e5c7d0, 0xd239, 0x47cb,
  { 0xaa, 0xcd, 0x7f, 0x66, 0xef, 0x76, 0x32, 0x38 }
};

STATIC
VOID
MdCopyEntryName (
  OUT CHAR8                 Name[MD_REGION_NAME_LEN + 1],
  IN  CONST MD_REGION_ENTRY *Entry
  )
{
  ZeroMem (Name, MD_REGION_NAME_LEN + 1);
  CopyMem (Name, Entry->Name, MD_REGION_NAME_LEN);
}

EFI_STATUS
MdTableLocateRoot (
  OUT EFI_PHYSICAL_ADDRESS *Address,
  OUT UINTN                *Bytes
  )
{
  QCOM_SMEM_PROTOCOL *Smem;
  EFI_STATUS         Status;
  UINT32             SmemBytes;
  VOID               *SmemAddress;

  if (Address == NULL || Bytes == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *Address = 0;
  *Bytes   = 0;
  Smem     = NULL;
  Status = gBS->LocateProtocol (
                  &mQcomSmemProtocolGuid,
                  NULL,
                  (VOID **)&Smem
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (Smem == NULL ||
      Smem->Revision < QCOM_SMEM_PROTOCOL_REVISION ||
      Smem->SmemGetAddr == NULL) {
    return EFI_UNSUPPORTED;
  }

  SmemBytes   = 0;
  SmemAddress = NULL;
  Status = Smem->SmemGetAddr (MD_SMEM_ITEM_ID, &SmemBytes, &SmemAddress);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (SmemAddress == NULL || SmemBytes < sizeof (MD_GLOBAL_TOC)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  *Address = (EFI_PHYSICAL_ADDRESS)(UINTN)SmemAddress;
  *Bytes   = SmemBytes;
  return EFI_SUCCESS;
}

EFI_STATUS
MdTableScanRoot (
  IN  EFI_PHYSICAL_ADDRESS Address,
  IN  UINTN                Bytes,
  OUT MD_TABLE_MAP         *Map
  )
{
  EFI_STATUS      Status;
  UINTN           Index;
  UINT64          ArrayBytes;
  MD_REGION_ENTRY Entry;

  if (Map == NULL || Address == 0) {
    return EFI_INVALID_PARAMETER;
  }
  if (Bytes < sizeof (MD_GLOBAL_TOC)) {
    return EFI_BAD_BUFFER_SIZE;
  }
  if (!MdRangeInOneReadableDescriptor (Address, sizeof (MD_GLOBAL_TOC))) {
    return EFI_ACCESS_DENIED;
  }

  Status = MdTableMapRoot (Address, Bytes, Map);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (!EFI_ERROR (MdDescribeAddress (Address, &Map->GtocMemory))) {
    Map->GtocDescribed = TRUE;
  }

  for (Index = 0; Index < Map->ArrayCount; ++Index) {
    if (Map->Arrays[Index].Count > MAX_UINT64 / MD_REGION_ENTRY_SIZE) {
      return EFI_BAD_BUFFER_SIZE;
    }
    ArrayBytes = (UINT64)Map->Arrays[Index].Count * MD_REGION_ENTRY_SIZE;
    if (Map->Arrays[Index].Base < MD_MIN_REGION_ADDRESS ||
        Map->Arrays[Index].Base >= MD_MAX_REGION_ADDRESS ||
        !MdRangeInOneReadableDescriptor (
           Map->Arrays[Index].Base,
           ArrayBytes
           )) {
      return EFI_ACCESS_DENIED;
    }

    if (!EFI_ERROR (
           MdDescribeAddress (
             Map->Arrays[Index].Base,
             &Map->Arrays[Index].BaseMemory
             )
           )) {
      Map->Arrays[Index].BaseDescribed = TRUE;
    }
    Map->Arrays[Index].TocDescribed = Map->GtocDescribed;
    CopyMem (
      &Map->Arrays[Index].TocMemory,
      &Map->GtocMemory,
      sizeof (Map->Arrays[Index].TocMemory)
      );

    Status = MdTableReadEntry (Map, Index, 0, &Entry);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    MdCopyEntryName (Map->Arrays[Index].FirstName, &Entry);

    Status = MdTableReadEntry (
               Map,
               Index,
               Map->Arrays[Index].Count - 1,
               &Entry
               );
    if (EFI_ERROR (Status)) {
      return Status;
    }
    MdCopyEntryName (Map->Arrays[Index].LastName, &Entry);
  }

  return EFI_SUCCESS;
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
