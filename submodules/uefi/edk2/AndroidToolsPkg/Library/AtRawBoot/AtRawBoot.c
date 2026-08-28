/** @file
 *  Raw file loading and cache-safe physical payload handoff.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include <Uefi.h>
#include <Guid/FileInfo.h>
#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>

#include <AtRawBoot.h>

EFI_STATUS
AtRawReadFile (
  IN  CONST CHAR16  *Path,
  OUT VOID          **Data,
  OUT UINTN         *Size
  )
{
  EFI_STATUS                       Status;
  EFI_LOADED_IMAGE_PROTOCOL       *LoadedImage;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *SimpleFileSystem;
  EFI_FILE_PROTOCOL               *Root;
  EFI_FILE_PROTOCOL               *File;
  EFI_FILE_INFO                   *FileInfo;
  UINTN                           FileInfoSize;
  UINTN                           ReadSize;
  VOID                            *FileData;

  if (Data == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *Data = NULL;
  if (Size == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Size = 0;
  if (Path == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  LoadedImage = NULL;
  SimpleFileSystem = NULL;
  Root = NULL;
  File = NULL;
  FileInfo = NULL;
  FileInfoSize = 0;
  ReadSize = 0;
  FileData = NULL;

  Status = gBS->HandleProtocol (gImageHandle,
                                &gEfiLoadedImageProtocolGuid,
                                (VOID **)&LoadedImage);
  if (EFI_ERROR (Status) || (LoadedImage == NULL) ||
      (LoadedImage->DeviceHandle == NULL)) {
    if (!EFI_ERROR (Status) && ((LoadedImage == NULL) ||
                                (LoadedImage->DeviceHandle == NULL))) {
      Status = EFI_NOT_FOUND;
    }
    goto Done;
  }

  Status = gBS->HandleProtocol (LoadedImage->DeviceHandle,
                                &gEfiSimpleFileSystemProtocolGuid,
                                (VOID **)&SimpleFileSystem);
  if (EFI_ERROR (Status) || (SimpleFileSystem == NULL)) {
    if (!EFI_ERROR (Status) && (SimpleFileSystem == NULL)) {
      Status = EFI_NOT_FOUND;
    }
    goto Done;
  }

  Status = SimpleFileSystem->OpenVolume (SimpleFileSystem, &Root);
  if (EFI_ERROR (Status) || (Root == NULL)) {
    if (!EFI_ERROR (Status) && (Root == NULL)) {
      Status = EFI_NOT_FOUND;
    }
    goto Done;
  }

  Status = Root->Open (Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status) || (File == NULL)) {
    if (!EFI_ERROR (Status) && (File == NULL)) {
      Status = EFI_NOT_FOUND;
    }
    goto Done;
  }

  Status = File->GetInfo (File, &gEfiFileInfoGuid, &FileInfoSize, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    if (!EFI_ERROR (Status)) {
      Status = EFI_DEVICE_ERROR;
    }
    goto Done;
  }

  FileInfo = AllocatePool (FileInfoSize);
  if (FileInfo == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }

  Status = File->GetInfo (File, &gEfiFileInfoGuid, &FileInfoSize, FileInfo);
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  if (FileInfo->FileSize > (UINT64)MAX_UINTN) {
    Status = EFI_BAD_BUFFER_SIZE;
    goto Done;
  }

  ReadSize = (UINTN)FileInfo->FileSize;
  if (ReadSize != 0) {
    FileData = AllocatePool (ReadSize);
    if (FileData == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      goto Done;
    }

    Status = File->Read (File, &ReadSize, FileData);
    if (EFI_ERROR (Status)) {
      goto Done;
    }

    if (ReadSize != (UINTN)FileInfo->FileSize) {
      Status = EFI_DEVICE_ERROR;
      goto Done;
    }
  }

  Status = EFI_SUCCESS;

Done:
  if (File != NULL) {
    File->Close (File);
  }
  if (Root != NULL) {
    Root->Close (Root);
  }
  if (FileInfo != NULL) {
    FreePool (FileInfo);
  }

  if (EFI_ERROR (Status)) {
    if (FileData != NULL) {
      FreePool (FileData);
    }
    return Status;
  }

  *Data = FileData;
  *Size = ReadSize;
  return EFI_SUCCESS;
}

EFI_STATUS
AtRawReserve (
  IN EFI_PHYSICAL_ADDRESS  Base,
  IN UINTN                 Size
  )
{
  EFI_STATUS Status;

  if (Size == 0) {
    return EFI_INVALID_PARAMETER;
  }

  Status = gBS->AllocatePages (AllocateAddress, EfiLoaderData,
                               EFI_SIZE_TO_PAGES (Size), &Base);
  if ((Status == EFI_NOT_FOUND) || (Status == EFI_OUT_OF_RESOURCES)) {
    return EFI_NOT_FOUND;
  }

  return Status;
}

VOID
AtRawJump (
  IN EFI_PHYSICAL_ADDRESS          Entry,
  IN UINT64                        Arg0,
  IN CONST EFI_PHYSICAL_ADDRESS    *Ranges,
  IN CONST UINTN                   *Sizes,
  IN UINTN                         RangeCount
  )
{
  EFI_STATUS               Status;
  UINTN                    MemoryMapSize;
  UINTN                    MemoryMapCapacity;
  UINTN                    DescriptorSize;
  UINT32                   DescriptorVersion;
  UINTN                    Index;
  UINTN                    MapKey;
  EFI_MEMORY_DESCRIPTOR    *MemoryMap;
  typedef VOID (*AT_RAW_ENTRY) (UINT64, UINT64, UINT64, UINT64);

  /* The watchdog must not reset the target while the payload owns execution. */
  gBS->SetWatchdogTimer (0, 0, 0, NULL);

  MemoryMap = NULL;
  MemoryMapCapacity = 0;
  MemoryMapSize = 0;
  DescriptorSize = 0;
  DescriptorVersion = 0;
  MapKey = 0;

  Status = gBS->GetMemoryMap (&MemoryMapSize, NULL, &MapKey,
                              &DescriptorSize, &DescriptorVersion);
  if ((Status != EFI_BUFFER_TOO_SMALL) || (DescriptorSize == 0) ||
      (DescriptorSize > (MAX_UINTN / 2)) ||
      (MemoryMapSize > (MAX_UINTN - (DescriptorSize * 2)))) {
    DEBUG ((DEBUG_ERROR, "AT: initial memory map query failed: %r\n", Status));
    Print (L"AtRawJump: memory map query failed (%r)\r\n", Status);
    for (;;) {
    }
  }

  MemoryMapCapacity = MemoryMapSize + DescriptorSize * 2;
  MemoryMap = AllocatePool (MemoryMapCapacity);
  if (MemoryMap == NULL) {
    DEBUG ((DEBUG_ERROR, "AT: memory map allocation failed\n"));
    Print (L"AtRawJump: memory map allocation failed\r\n");
    for (;;) {
    }
  }

  MemoryMapSize = MemoryMapCapacity;
  Status = gBS->GetMemoryMap (&MemoryMapSize, MemoryMap, &MapKey,
                              &DescriptorSize, &DescriptorVersion);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AT: memory map read failed: %r\n", Status));
    Print (L"AtRawJump: memory map read failed (%r)\r\n", Status);
    for (;;) {
    }
  }

  Status = gBS->ExitBootServices (gImageHandle, MapKey);
  if (Status == EFI_INVALID_PARAMETER) {
    /* No allocation is allowed between this map refresh and the retry. */
    MemoryMapSize = MemoryMapCapacity;
    Status = gBS->GetMemoryMap (&MemoryMapSize, MemoryMap, &MapKey,
                                &DescriptorSize, &DescriptorVersion);
    if (!EFI_ERROR (Status)) {
      Status = gBS->ExitBootServices (gImageHandle, MapKey);
    }
  }

  if (EFI_ERROR (Status)) {
    /* Returning would resume UEFI with a payload already copied over physical memory. */
    DEBUG ((DEBUG_ERROR, "AT: ExitBootServices failed after retry: %r\n", Status));
    Print (L"AtRawJump: ExitBootServices failed (%r)\r\n", Status);
    for (;;) {
    }
  }

  if ((Ranges != NULL) && (Sizes != NULL) && (RangeCount != 0)) {
    for (Index = 0; Index < RangeCount; Index++) {
      WriteBackInvalidateDataCacheRange ((VOID *)(UINTN)Ranges[Index],
                                         Sizes[Index]);
    }
  }

  ArmDisableCachesAndMmu ();
  ArmInvalidateInstructionCache ();
  ((AT_RAW_ENTRY)(UINTN)Entry) (Arg0, 0, 0, 0);
  for (;;) {
  }
}
