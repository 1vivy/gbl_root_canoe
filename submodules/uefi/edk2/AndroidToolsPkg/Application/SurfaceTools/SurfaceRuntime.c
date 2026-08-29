/** @file
 *  Passive image, memory-map and execution-context inventory for SurfaceTools.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#if defined (MDE_CPU_AARCH64)
#include <Library/ArmLib.h>
#endif
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/DebugSupport.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/Security.h>
#include <Protocol/Security2.h>

#include "SurfaceInventory.h"

#define ST_MAX_MEMORY_ROWS  512u
#define ST_MAX_IMAGE_ROWS   256u

typedef struct {
  EFI_PHYSICAL_ADDRESS Base;
  UINT64               Size;
  EFI_MEMORY_TYPE      CodeType;
  EFI_MEMORY_TYPE      DataType;
} ST_IMAGE;

STATIC EFI_STATUS
GetMemoryMapCopy (
  OUT EFI_MEMORY_DESCRIPTOR **Map,
  OUT UINTN                  *MapSize,
  OUT UINTN                  *DescriptorSize
  )
{
  EFI_STATUS Status;
  UINTN Key;
  UINT32 Version;
  UINTN Attempt;
  UINTN Extra;

  if (Map == NULL || MapSize == NULL || DescriptorSize == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Map = NULL;
  *MapSize = 0;
  *DescriptorSize = 0;

  Status = gBS->GetMemoryMap (MapSize, NULL, &Key, DescriptorSize, &Version);
  if (Status != EFI_BUFFER_TOO_SMALL || *DescriptorSize < sizeof (**Map)) {
    return EFI_ERROR (Status) ? Status : EFI_COMPROMISED_DATA;
  }

  for (Attempt = 0; Attempt < 3; Attempt++) {
    if (*DescriptorSize > MAX_UINTN / 8 ||
        *MapSize > MAX_UINTN - *DescriptorSize * 8) {
      return EFI_OUT_OF_RESOURCES;
    }
    Extra = *DescriptorSize * 8;
    *MapSize += Extra;
    *Map = AllocatePool (*MapSize);
    if (*Map == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    Status = gBS->GetMemoryMap (MapSize, *Map, &Key, DescriptorSize, &Version);
    if (Status != EFI_BUFFER_TOO_SMALL) {
      return Status;
    }
    FreePool (*Map);
    *Map = NULL;
  }
  return EFI_BUFFER_TOO_SMALL;
}

STATIC UINTN
CountHandles (EFI_GUID *Guid)
{
  EFI_HANDLE *Handles;
  EFI_STATUS Status;
  UINTN Count;

  Handles = NULL;
  Count = 0;
  Status = gBS->LocateHandleBuffer ((Guid == NULL) ? AllHandles : ByProtocol,
                                    Guid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return 0;
  }
  FreePool (Handles);
  return Count;
}

EFI_STATUS
StBuildSummaryReport (OUT AT_REPORT *Report)
{
  EFI_MEMORY_DESCRIPTOR *Map;
  EFI_STATUS Status;
  UINTN MapSize;
  UINTN DescriptorSize;
  UINTN MemoryCount;

  Status = AtReportInit (Report, 12);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Map = NULL;
  MapSize = 0;
  DescriptorSize = 0;
  Status = GetMemoryMapCopy (&Map, &MapSize, &DescriptorSize);
  MemoryCount = (!EFI_ERROR (Status) && DescriptorSize != 0) ?
                MapSize / DescriptorSize : 0;
  if (Map != NULL) {
    FreePool (Map);
  }

  AtReportAdd (Report, L"Mode: passive; no vendor methods invoked");
  AtReportAdd (Report, L"Firmware: %s", (gST->FirmwareVendor != NULL) ?
               gST->FirmwareVendor : L"<unknown>");
  AtReportAdd (Report, L"UEFI revision: %u.%u",
               gST->Hdr.Revision >> 16, gST->Hdr.Revision & 0xffff);
#if defined (MDE_CPU_AARCH64)
  AtReportAdd (Report, L"CurrentEL: EL%u", (UINT32)(ArmReadCurrentEL () >> 2));
  AtReportAdd (Report, L"VBAR configured: %s",
               (ArmReadVBar () != 0) ? L"yes" : L"no");
  AtReportAdd (Report, L"SCTLR: 0x%08x", ArmReadSctlr ());
#else
  AtReportAdd (Report, L"Architecture: non-AArch64 smoke target");
  AtReportAdd (Report, L"VBAR configured: n/a");
  AtReportAdd (Report, L"SCTLR: n/a");
#endif
  AtReportAdd (Report, L"Handles: %Lu", (UINT64)CountHandles (NULL));
  AtReportAdd (Report, L"Configuration tables: %Lu",
               (UINT64)gST->NumberOfTableEntries);
  AtReportAdd (Report, L"Loaded images: %Lu",
               (UINT64)CountHandles (&gEfiLoadedImageProtocolGuid));
  AtReportAdd (Report, L"Memory descriptors: %Lu", (UINT64)MemoryCount);
  AtReportAdd (Report, L"Security2 handles: %Lu",
               (UINT64)CountHandles (&gEfiSecurity2ArchProtocolGuid));
  AtReportAdd (Report, L"DebugSupport handles: %Lu",
               (UINT64)CountHandles (&gEfiDebugSupportProtocolGuid));
  return EFI_SUCCESS;
}

STATIC VOID
SortImages (ST_IMAGE *Images, UINTN Count)
{
  UINTN I;
  UINTN J;
  ST_IMAGE Swap;
  for (I = 1; I < Count; I++) {
    for (J = I; J > 0 && Images[J - 1].Base > Images[J].Base; J--) {
      Swap = Images[J - 1];
      Images[J - 1] = Images[J];
      Images[J] = Swap;
    }
  }
}

EFI_STATUS
StBuildImageReport (OUT AT_REPORT *Report)
{
  EFI_HANDLE *Handles;
  EFI_LOADED_IMAGE_PROTOCOL *Loaded;
  ST_IMAGE *Images;
  EFI_STATUS Status;
  UINTN HandleCount;
  UINTN Count;
  UINTN I;

  Handles = NULL;
  HandleCount = 0;
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiLoadedImageProtocolGuid,
                                    NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status) || HandleCount == 0) {
    Status = AtReportInit (Report, 1);
    if (!EFI_ERROR (Status)) {
      AtReportAdd (Report, L"No loaded-image handles");
    }
    return Status;
  }
  Count = (HandleCount < ST_MAX_IMAGE_ROWS) ? HandleCount : ST_MAX_IMAGE_ROWS;
  Status = AtReportInit (Report, Count);
  if (EFI_ERROR (Status)) {
    FreePool (Handles);
    return Status;
  }
  if (HandleCount > Count) {
    Report->Truncated = TRUE;
  }
  Images = AllocateZeroPool (Count * sizeof (*Images));
  if (Images == NULL) {
    FreePool (Handles);
    AtReportFree (Report);
    return EFI_OUT_OF_RESOURCES;
  }

  Count = 0;
  for (I = 0; I < HandleCount && Count < ST_MAX_IMAGE_ROWS; I++) {
    Loaded = NULL;
    Status = gBS->HandleProtocol (Handles[I], &gEfiLoadedImageProtocolGuid,
                                  (VOID **)&Loaded);
    if (EFI_ERROR (Status) || Loaded == NULL) {
      continue;
    }
    Images[Count].Base = (EFI_PHYSICAL_ADDRESS)(UINTN)Loaded->ImageBase;
    Images[Count].Size = Loaded->ImageSize;
    Images[Count].CodeType = Loaded->ImageCodeType;
    Images[Count].DataType = Loaded->ImageDataType;
    Count++;
  }
  FreePool (Handles);

  SortImages (Images, Count);
  for (I = 0; I < Count; I++) {
    AtReportAdd (Report, L"%03Lu size=%lx code=%u data=%u", (UINT64)I,
                 Images[I].Size, Images[I].CodeType, Images[I].DataType);
  }
  FreePool (Images);
  return EFI_SUCCESS;
}

STATIC CONST CHAR16 *
MemoryTypeName (EFI_MEMORY_TYPE Type)
{
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
  default: return L"unknown";
  }
}

EFI_STATUS
StBuildMemoryReport (OUT AT_REPORT *Report)
{
  EFI_MEMORY_DESCRIPTOR *Map;
  EFI_MEMORY_DESCRIPTOR *Descriptor;
  EFI_STATUS Status;
  UINTN MapSize;
  UINTN DescriptorSize;
  UINTN Count;
  UINTN I;

  Map = NULL;
  Status = GetMemoryMapCopy (&Map, &MapSize, &DescriptorSize);
  if (EFI_ERROR (Status)) {
    if (Map != NULL) {
      FreePool (Map);
    }
    return Status;
  }
  Count = MapSize / DescriptorSize;
  Status = AtReportInit (Report, (Count == 0) ? 1 :
                        ((Count < ST_MAX_MEMORY_ROWS) ? Count : ST_MAX_MEMORY_ROWS));
  if (EFI_ERROR (Status)) {
    FreePool (Map);
    return Status;
  }
  if (Count > ST_MAX_MEMORY_ROWS) {
    Count = ST_MAX_MEMORY_ROWS;
    Report->Truncated = TRUE;
  }
  if (Count == 0) {
    AtReportAdd (Report, L"Memory map is empty");
  }
  for (I = 0; I < Count; I++) {
    Descriptor = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + I * DescriptorSize);
    AtReportAdd (Report, L"%03Lu %-12s pages=%lx attr=%lx", (UINT64)I,
                 MemoryTypeName (Descriptor->Type),
                 Descriptor->NumberOfPages, Descriptor->Attribute);
  }
  FreePool (Map);
  return EFI_SUCCESS;
}
