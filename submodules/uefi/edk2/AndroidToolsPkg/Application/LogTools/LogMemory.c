/** @file
 *  Bounded memory-map validation and Qualcomm info-block HOB access.
 *  Firmware addresses are data, not authority. Copies require a non-MMIO
 *  descriptor and are capped so malformed firmware metadata cannot fault us.
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Guid/HobList.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Pi/PiBootMode.h>
#include <Pi/PiHob.h>
#include "LogTools.h"
STATIC CONST EFI_GUID mEfiInfoBlkHobGuid = {
  0x90a49afd, 0x422f, 0x08ae,
  { 0x96, 0x11, 0xe7, 0x88, 0xd3, 0x80, 0x48, 0x45 }
};
STATIC
EFI_STATUS
LtGetMemoryMapCopy (
  OUT EFI_MEMORY_DESCRIPTOR **Map,
  OUT UINTN                  *MapSize,
  OUT UINTN                  *DescriptorSize
  )
{
  EFI_STATUS Status;
  UINTN      Key;
  UINT32     Version;
  UINTN      Attempt;
  UINTN      Size;

  if (Map == NULL || MapSize == NULL || DescriptorSize == NULL || gBS == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Map = NULL;
  *MapSize = 0;
  *DescriptorSize = 0;
  Size = 0;
  Status = gBS->GetMemoryMap (&Size, NULL, &Key, DescriptorSize, &Version);
  if (Status != EFI_BUFFER_TOO_SMALL ||
      *DescriptorSize < sizeof (EFI_MEMORY_DESCRIPTOR)) {
    return EFI_ERROR (Status) ? Status : EFI_COMPROMISED_DATA;
  }
  for (Attempt = 0; Attempt < 4; Attempt++) {
    if (*DescriptorSize > MAX_UINTN / 8 ||
        Size > MAX_UINTN - *DescriptorSize * 8) {
      return EFI_OUT_OF_RESOURCES;
    }
    Size += *DescriptorSize * 8;
    *Map = AllocatePool (Size);
    if (*Map == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    *MapSize = Size;
    Status = gBS->GetMemoryMap (MapSize, *Map, &Key, DescriptorSize, &Version);
    if (Status == EFI_SUCCESS) {
      return EFI_SUCCESS;
    }
    FreePool (*Map);
    *Map = NULL;
    if (Status != EFI_BUFFER_TOO_SMALL) {
      return Status;
    }
    Size = *MapSize;
  }
  return EFI_BUFFER_TOO_SMALL;
}

STATIC
BOOLEAN
LtReadableType (
  IN EFI_MEMORY_TYPE Type
  )
{
  switch (Type) {
    case EfiReservedMemoryType:
    case EfiLoaderCode:
    case EfiLoaderData:
    case EfiBootServicesCode:
    case EfiBootServicesData:
    case EfiRuntimeServicesCode:
    case EfiRuntimeServicesData:
    case EfiConventionalMemory:
    case EfiACPIReclaimMemory:
    case EfiACPIMemoryNVS:
    case EfiPalCode:
    case EfiPersistentMemory:
      return TRUE;
    default:
      return FALSE;
  }
}

STATIC
BOOLEAN
LtReadableRange (
  IN EFI_PHYSICAL_ADDRESS Address,
  IN UINTN                Length
  )
{
  EFI_MEMORY_DESCRIPTOR *Map;
  EFI_MEMORY_DESCRIPTOR *Descriptor;
  EFI_STATUS             Status;
  UINTN                  MapSize;
  UINTN                  DescriptorSize;
  UINTN                  Index;
  UINTN                  Count;
  UINT64                 End;
  UINT64                 Base;
  UINT64                 Span;

  if (Length == 0 || Length > LT_MAX_PHYSICAL_READ ||
      Address > (EFI_PHYSICAL_ADDRESS)MAX_UINT64 - Length) {
    return FALSE;
  }
  End = (UINT64)Address + Length;
  Map = NULL;
  Status = LtGetMemoryMapCopy (&Map, &MapSize, &DescriptorSize);
  if (EFI_ERROR (Status) || Map == NULL || DescriptorSize == 0) {
    return FALSE;
  }
  Count = MapSize / DescriptorSize;
  for (Index = 0; Index < Count; Index++) {
    Descriptor = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map +
                                           Index * DescriptorSize);
    if (!LtReadableType ((EFI_MEMORY_TYPE)Descriptor->Type) ||
        Descriptor->NumberOfPages > MAX_UINT64 / EFI_PAGE_SIZE) {
      continue;
    }
    Base = (UINT64)Descriptor->PhysicalStart;
    Span = Descriptor->NumberOfPages * EFI_PAGE_SIZE;
    if (Base <= MAX_UINT64 - Span &&
        (UINT64)Address >= Base && End <= Base + Span) {
      FreePool (Map);
      return TRUE;
    }
  }
  FreePool (Map);
  return FALSE;
}

EFI_STATUS
LtCopyPhysical (
  IN EFI_PHYSICAL_ADDRESS Address,
  OUT VOID                *Buffer,
  IN UINTN                 Length
  )
{
  if (Buffer == NULL || Length == 0 || Length > LT_MAX_PHYSICAL_READ ||
      Address > MAX_UINTN || !LtReadableRange (Address, Length)) {
    return EFI_ACCESS_DENIED;
  }
  CopyMem (Buffer, (VOID *)(UINTN)Address, Length);
  return EFI_SUCCESS;
}

EFI_STATUS
LtReadInfoBlock (
  OUT LT_INFO_BLOCK *Info
  )
{
  EFI_STATUS Status;
  VOID       *Block = NULL;
  UINT8      Prefix[LT_INFO_BLOCK_SIZE];

  if (Info == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (Info, sizeof (*Info));
  Status = LtReadGuidHobPointer (&mEfiInfoBlkHobGuid, &Block);
  Info->Status = Status;
  /* Anything other than a plain absence means the HOB itself was located and
     the failure is about what it pointed at - worth telling apart in the
     report, because the two have different causes. */
  Info->Found = (BOOLEAN)(Status != EFI_NOT_FOUND);
  if (EFI_ERROR (Status) || Block == NULL) {
    return Status;
  }
  Info->Address = (EFI_PHYSICAL_ADDRESS)(UINTN)Block;
  Status = LtCopyPhysical (Info->Address, Prefix, sizeof (Prefix));
  if (EFI_ERROR (Status)) {
    Info->Status = Status;
    return Status;
  }
  CopyMem (&Info->Signature, Prefix + 0x00, sizeof (Info->Signature));
  CopyMem (&Info->StructVersion, Prefix + 0x04, sizeof (Info->StructVersion));
  CopyMem (&Info->UartLogBufferPtr, Prefix + 0x38,
           sizeof (Info->UartLogBufferPtr));
  CopyMem (&Info->UartLogBufferLen, Prefix + 0x40,
           sizeof (Info->UartLogBufferLen));
  Info->Readable = TRUE;
  Info->Status = EFI_SUCCESS;
  return EFI_SUCCESS;
}
