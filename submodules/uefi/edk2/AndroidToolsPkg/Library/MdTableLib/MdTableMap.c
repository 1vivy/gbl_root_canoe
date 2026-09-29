/** @file
  UEFI memory-map range gates and shared minidump entry helpers.

  No function in this file dereferences every word of a descriptor. Descriptor
  iteration is used only for containment checks and CrashTools hole selection.

  Copyright (c) 2026, contributors to the canoe ABL tree.
  SPDX-License-Identifier: BSD-3-Clause
**/
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "MdTableLibInternal.h"

BOOLEAN
MdMemoryTypeReadable (
  IN EFI_MEMORY_TYPE Type,
  IN BOOLEAN         IncludeConventional
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
    case EfiACPIReclaimMemory:
    case EfiACPIMemoryNVS:
      return TRUE;
    case EfiConventionalMemory:
      return IncludeConventional;
    default:
      return FALSE;
  }
}

EFI_STATUS
MdMapWalkInit (
  IN OUT MD_MAP_WALK *Walk,
  IN     BOOLEAN     IncludeConventional
  )
{
  EFI_STATUS Status;
  UINTN      Key;
  UINT32     Version;
  UINTN      Attempt;

  if (Walk == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (Walk, sizeof (*Walk));
  Walk->IncludeConventional = IncludeConventional;
  Walk->MapSize = 0;
  Status = gBS->GetMemoryMap (&Walk->MapSize, NULL, &Key,
                              &Walk->DescriptorSize, &Version);
  if (Status != EFI_BUFFER_TOO_SMALL ||
      Walk->DescriptorSize < sizeof (EFI_MEMORY_DESCRIPTOR)) {
    return EFI_ERROR (Status) ? Status : EFI_COMPROMISED_DATA;
  }
  for (Attempt = 0; Attempt < 4; Attempt++) {
    if (Walk->DescriptorSize > MAX_UINTN / 8 ||
        Walk->MapSize > MAX_UINTN - Walk->DescriptorSize * 8) {
      return EFI_OUT_OF_RESOURCES;
    }
    Walk->MapSize += Walk->DescriptorSize * 8;
    Walk->Map = AllocatePool (Walk->MapSize);
    if (Walk->Map == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    Status = gBS->GetMemoryMap (&Walk->MapSize, Walk->Map, &Key,
                                &Walk->DescriptorSize, &Version);
    if (Status == EFI_SUCCESS) {
      return EFI_SUCCESS;
    }
    FreePool (Walk->Map);
    Walk->Map = NULL;
    if (Status != EFI_BUFFER_TOO_SMALL) {
      return Status;
    }
  }
  return EFI_BUFFER_TOO_SMALL;
}

BOOLEAN
MdMapWalkNext (
  IN OUT MD_MAP_WALK *Walk,
  OUT    UINT64      *Base,
  OUT    UINT64      *Size
  )
{
  EFI_MEMORY_DESCRIPTOR *Descriptor;
  UINTN                 Count;
  UINT64                Span;

  if (Walk == NULL || Walk->Map == NULL || Walk->DescriptorSize == 0 ||
      Walk->Scanned >= MD_SCAN_BUDGET_BYTES) {
    return FALSE;
  }
  Count = Walk->MapSize / Walk->DescriptorSize;
  for (; Walk->Index < Count; Walk->Index++) {
    Descriptor = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Walk->Map +
                                           Walk->Index * Walk->DescriptorSize);
    if (!MdMemoryTypeReadable ((EFI_MEMORY_TYPE)Descriptor->Type,
                               Walk->IncludeConventional) ||
        Descriptor->NumberOfPages > MAX_UINT64 / EFI_PAGE_SIZE) {
      continue;
    }
    Span = Descriptor->NumberOfPages * EFI_PAGE_SIZE;
    if ((UINT64)Descriptor->PhysicalStart > MAX_UINT64 - Span) {
      continue;
    }
    Walk->Index++;
    *Base = (UINT64)Descriptor->PhysicalStart;
    *Size = Span;
    Walk->Scanned += Span;
    return TRUE;
  }
  return FALSE;
}

VOID
MdMapWalkFree (
  IN OUT MD_MAP_WALK *Walk
  )
{
  if (Walk != NULL && Walk->Map != NULL) {
    FreePool (Walk->Map);
    Walk->Map = NULL;
  }
}

EFI_STATUS
MdMapFindDescriptor (
  IN  CONST MD_MAP_WALK *Walk,
  IN  UINT64            Address,
  OUT MD_MEMORY_INFO    *Info
  )
{
  EFI_MEMORY_DESCRIPTOR *Descriptor;
  UINTN                 Count;
  UINTN                 Index;
  UINT64                Span;

  if (Walk == NULL || Info == NULL || Walk->Map == NULL ||
      Walk->DescriptorSize < sizeof (EFI_MEMORY_DESCRIPTOR)) {
    return EFI_INVALID_PARAMETER;
  }
  Count = Walk->MapSize / Walk->DescriptorSize;
  for (Index = 0; Index < Count; Index++) {
    Descriptor = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Walk->Map +
                                           Index * Walk->DescriptorSize);
    if (Descriptor->NumberOfPages > MAX_UINT64 / EFI_PAGE_SIZE) {
      continue;
    }
    Span = Descriptor->NumberOfPages * EFI_PAGE_SIZE;
    /* Containment without overflow: Address - Start < Span only when
       Address is at or after Start, so a wrapped end cannot match. */
    if (Span == 0 || (UINT64)Descriptor->PhysicalStart > Address ||
        Address - (UINT64)Descriptor->PhysicalStart >= Span) {
      continue;
    }
    Info->Base = (UINT64)Descriptor->PhysicalStart;
    Info->Size = Span;
    Info->Type = (EFI_MEMORY_TYPE)Descriptor->Type;
    Info->Attributes = (UINT64)Descriptor->Attribute;
    return EFI_SUCCESS;
  }
  return EFI_NOT_FOUND;
}

EFI_STATUS
MdDescribeAddress (
  IN  UINT64         Address,
  OUT MD_MEMORY_INFO *Info
  )
{
  MD_MAP_WALK Walk;
  EFI_STATUS  Status;

  if (Info == NULL || Address > MAX_UINTN) {
    return EFI_INVALID_PARAMETER;
  }
  /* IncludeConventional is irrelevant to a descriptor lookup - nothing is
     read through this snapshot - so a FALSE walk still sees every type. */
  Status = MdMapWalkInit (&Walk, FALSE);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = MdMapFindDescriptor (&Walk, Address, Info);
  MdMapWalkFree (&Walk);
  return Status;
}

BOOLEAN
MdRangeInOneReadableDescriptor (
  IN UINT64 Address,
  IN UINT64 Bytes
  )
{
  MD_MAP_WALK    Walk;
  MD_MEMORY_INFO Head;
  MD_MEMORY_INFO Tail;
  UINT64         Last;
  BOOLEAN        Inside;

  if (Bytes == 0 || Address > MAX_UINT64 - (Bytes - 1)) {
    return FALSE;
  }
  Last = Address + Bytes - 1;
  if (EFI_ERROR (MdMapWalkInit (&Walk, FALSE))) {
    return FALSE;
  }
  Inside = FALSE;
  if (!EFI_ERROR (MdMapFindDescriptor (&Walk, Address, &Head)) &&
      !EFI_ERROR (MdMapFindDescriptor (&Walk, Last, &Tail)) &&
      Head.Base == Tail.Base && Head.Size == Tail.Size &&
      MdMemoryTypeReadable (Head.Type, FALSE)) {
    Inside = TRUE;
  }
  MdMapWalkFree (&Walk);
  return Inside;
}


BOOLEAN
MdRangeWritable (
  IN UINT64 Address,
  IN UINT64 Bytes
  )
{
  MD_MAP_WALK    Walk;
  MD_MEMORY_INFO Info;
  UINT64         End;
  UINT64         Next;
  BOOLEAN        Writable;

  if (Bytes == 0 || Address > MAX_UINT64 - Bytes) {
    return FALSE;
  }
  End = Address + Bytes;
  if (EFI_ERROR (MdMapWalkInit (&Walk, FALSE))) {
    /* Fail closed: with no memory map, nothing is known to be writable. */
    return FALSE;
  }
  Writable = FALSE;
  while (Address < End) {
    /* A range that starts writable and ends read-only is not writable, so the
       walk advances descriptor by descriptor to the end of the range instead
       of trusting the descriptor that covers only the first byte. */
    if (EFI_ERROR (MdMapFindDescriptor (&Walk, Address, &Info)) ||
        !MdMemoryTypeReadable (Info.Type, FALSE) ||
        (Info.Attributes & (EFI_MEMORY_RO | EFI_MEMORY_WP)) != 0 ||
        Info.Size == 0) {
      break;
    }
    Next = Info.Base + Info.Size;
    if (Next <= Address) {
      break;
    }
    Address = Next;
    Writable = (BOOLEAN)(Address >= End);
  }
  MdMapWalkFree (&Walk);
  return Writable;
}



BOOLEAN
MdEntryPlausible (
  IN EFI_PHYSICAL_ADDRESS Address,
  OUT MD_REGION_ENTRY     *Entry OPTIONAL
  )
{
  MD_REGION_ENTRY Candidate;
  UINTN           Index;
  BOOLEAN         Printable;

  if (Address > MAX_UINTN ||
      Address > (EFI_PHYSICAL_ADDRESS)MAX_UINT64 - sizeof (Candidate)) {
    return FALSE;
  }
  CopyMem (&Candidate, (VOID *)(UINTN)Address, sizeof (Candidate));
  /* valid == 'VALI' and seq_num != UINT32_MAX are collector predicates.
     Payload address and size bounds are this tool's fail-closed validation for
     entries reached through a bounded live subsystem array. */
  if (Candidate.Valid != MD_REGION_VALID_VALUE ||
      Candidate.SeqNum == MAX_UINT32 ||
      Candidate.Address < MD_MIN_REGION_ADDRESS ||
      Candidate.Address >= MD_MAX_REGION_ADDRESS ||
      Candidate.Size == 0 || Candidate.Size > MD_MAX_REGION_SIZE ||
      Candidate.Name[0] < 0x21 || Candidate.Name[0] > 0x7e) {
    return FALSE;
  }
  /* Remaining name bytes must be printable or NUL padding. */
  Printable = TRUE;
  for (Index = 1; Index < MD_REGION_NAME_LEN; Index++) {
    if (Candidate.Name[Index] == '\0') {
      break;
    }
    if (Candidate.Name[Index] < 0x20 || Candidate.Name[Index] > 0x7e) {
      Printable = FALSE;
      break;
    }
  }
  if (!Printable) {
    return FALSE;
  }
  if (Entry != NULL) {
    CopyMem (Entry, &Candidate, sizeof (Candidate));
  }
  return TRUE;
}

EFI_STATUS
MdFindUnmappedAddress (
  OUT UINT64 *Address
  )
{
  MD_MAP_WALK Walk;
  EFI_STATUS  Status;
  UINT64      Base;
  UINT64      Size;
  UINT64      Top;

  if (Address == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Status = MdMapWalkInit (&Walk, TRUE);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Top = 0;
  while (MdMapWalkNext (&Walk, &Base, &Size)) {
    if (Base + Size > Top) {
      Top = Base + Size;
    }
  }
  MdMapWalkFree (&Walk);
  if (Top == 0 || Top > MAX_UINT64 - 0x200000) {
    return EFI_NOT_FOUND;
  }
  *Address = (Top + 0x100000) & ~0xFFFULL;
  return EFI_SUCCESS;
}


CONST CHAR16 *
MdMemoryTypeName (
  IN EFI_MEMORY_TYPE Type
  )
{
  /* Labels match the ones SurfaceTools already prints, so a reader comparing
     the two reports sees the same words for the same type. */
  switch (Type) {
  case EfiReservedMemoryType: return L"reserved";
  case EfiLoaderCode: return L"loader-code";
  case EfiLoaderData: return L"loader-data";
  case EfiBootServicesCode: return L"bs-code";
  case EfiBootServicesData: return L"bs-data";
  case EfiRuntimeServicesCode: return L"rt-code";
  case EfiRuntimeServicesData: return L"rt-data";
  case EfiConventionalMemory: return L"conventional";
  case EfiUnusableMemory: return L"unusable";
  case EfiACPIReclaimMemory: return L"acpi-reclaim";
  case EfiACPIMemoryNVS: return L"acpi-nvs";
  case EfiMemoryMappedIO: return L"mmio";
  case EfiMemoryMappedIOPortSpace: return L"mmio-port";
  case EfiPalCode: return L"pal-code";
  case EfiPersistentMemory: return L"persistent";
  default: return L"unknown";
  }
}
