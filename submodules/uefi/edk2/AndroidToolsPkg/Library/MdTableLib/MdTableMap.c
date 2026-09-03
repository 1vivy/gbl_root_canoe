/** @file
 *  Memory-map walk and the needle-scan driver shared by the anchor pass and
 *  the subsystem back-reference pass.
 *
 *  Reads stay inside descriptors the UEFI memory map marks as DRAM-like;
 *  EfiMemoryMappedIO is never touched, so a scan has no register side effects.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "MdTableLibInternal.h"

STATIC
BOOLEAN
MdReadableType (
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
    if (!MdReadableType ((EFI_MEMORY_TYPE)Descriptor->Type,
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
MdScanPass (
  IN     CONST UINT64 *Needles,
  IN     UINTN        NeedleCount,
  IN     MD_SCAN_HIT_FN OnHit,
  IN OUT VOID         *Context,
  IN     BOOLEAN      IncludeConventional,
  OUT    UINT64       *BytesScanned
  )
{
  MD_MAP_WALK  Walk;
  EFI_STATUS   Status;
  UINT64       Base;
  UINT64       Size;
  UINT64       Offset;
  UINT64       End;
  UINT64       Value;
  UINTN        Index;
  BOOLEAN      Hit;
  BOOLEAN      KeepGoing;

  if (Needles == NULL || NeedleCount == 0 || OnHit == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Status = MdMapWalkInit (&Walk, IncludeConventional);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Hit = FALSE;
  KeepGoing = TRUE;
  while (KeepGoing && MdMapWalkNext (&Walk, &Base, &Size)) {
    if (Size < sizeof (UINT64)) {
      continue;
    }
    End = Base + Size - sizeof (UINT64);
    for (Offset = Base; Offset <= End; Offset += sizeof (UINT64)) {
      Value = *(volatile UINT64 *)(UINTN)Offset;
      for (Index = 0; Index < NeedleCount; Index++) {
        if (Value == Needles[Index]) {
          Hit = TRUE;
          if (!OnHit ((EFI_PHYSICAL_ADDRESS)Offset, Index, Context)) {
            KeepGoing = FALSE;
          }
          break;
        }
      }
      if (!KeepGoing) {
        break;
      }
    }
  }
  if (BytesScanned != NULL) {
    *BytesScanned = Walk.Scanned;
  }
  Status = (Walk.Scanned >= MD_SCAN_BUDGET_BYTES) ? EFI_BUFFER_TOO_SMALL
                                                  : EFI_SUCCESS;
  MdMapWalkFree (&Walk);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    return Status;
  }
  return Hit ? EFI_SUCCESS : EFI_NOT_FOUND;
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
  if (Candidate.Valid != MD_REGION_VALID_VALUE ||
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
