/*
 * Initrd and device-tree publication for a Linux EFI-stub kernel.
 * See SuperFbLinuxBoot.h.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbLinuxBoot.h"
#include "SuperFbMenu.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Guid/FileInfo.h>
#include <Library/UefiLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/LoadFile2.h>

/* Keeps the translation unit legal when the feature is compiled out. */
CONST CHAR8 *gSfbLinuxBootModuleTag = "SuperFbLinuxBoot";

/*
 * LINUX_EFI_INITRD_MEDIA_GUID. The arm64 stub looks for exactly this
 * vendor-media device path and loads whatever the LoadFile2 behind it hands
 * over; nothing else about the handle matters to it.
 */
STATIC CONST EFI_GUID mSfbInitrdMediaGuid = {
  0x5568e427, 0x68fc, 0x4f3d,
  { 0xac, 0x74, 0xca, 0x55, 0x52, 0x31, 0xcc, 0x68 }
};

/* EFI_DTB_TABLE_GUID, DEVICE_TREE_GUID on the kernel side. */
STATIC CONST EFI_GUID mSfbDtbTableGuid = {
  0xb1b621d5, 0xf19c, 0x41a5,
  { 0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0 }
};

#define SFB_FDT_MAGIC  0xd00dfeedu

/* The device path the stub matches: one vendor-media node and an end node. */
#pragma pack(1)
typedef struct {
  VENDOR_DEVICE_PATH        Vendor;
  EFI_DEVICE_PATH_PROTOCOL  End;
} SFB_INITRD_DEVICE_PATH;
#pragma pack()

STATIC SFB_INITRD_DEVICE_PATH  mSfbInitrdPath;
STATIC EFI_LOAD_FILE2_PROTOCOL mSfbInitrdLoadFile;
STATIC EFI_HANDLE              mSfbInitrdHandle = NULL;
STATIC VOID                    *mSfbInitrdData = NULL;
STATIC UINTN                   mSfbInitrdSize = 0;

STATIC VOID   *mSfbDtb = NULL;
STATIC UINTN  mSfbDtbPages = 0;

/* Big-endian load: every field in an FDT header is stored that way. */
STATIC
UINT32
SfbFdtBe32 (IN CONST UINT8 *Bytes)
{
  return ((UINT32)Bytes[0] << 24) | ((UINT32)Bytes[1] << 16) |
         ((UINT32)Bytes[2] << 8) | (UINT32)Bytes[3];
}

/*
 * Read a whole file from a volume into pool memory, sized from EFI_FILE_INFO
 * rather than from a caller's guess.
 *
 * This is not SfbReadFileBytes: that one takes a fixed caller buffer and
 * treats a short read as success, which is right for a config file and wrong
 * for a payload, where a truncated image is executed rather than rejected.
 */
STATIC
EFI_STATUS
SfbLinuxReadWholeFile (IN EFI_HANDLE       Volume,
                       IN CONST CHAR16     *Path,
                       OUT VOID            **Data,
                       OUT UINTN           *Size)
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Root = NULL;
  EFI_FILE_PROTOCOL  *File = NULL;
  EFI_FILE_INFO      *Info = NULL;
  UINTN              InfoSize;
  UINTN              Bytes;
  VOID               *Buffer = NULL;

  *Data = NULL;
  *Size = 0;

  Status = SfbOpenVolumeRoot (Volume, &Root);
  if (EFI_ERROR (Status) || Root == NULL) {
    return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;
  }

  Status = Root->Open (Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status) || File == NULL) {
    Root->Close (Root);
    return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;
  }

  InfoSize = 0;
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    Status = EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
    goto Done;
  }
  Info = AllocateZeroPool (InfoSize);
  if (Info == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, Info);
  if (EFI_ERROR (Status)) {
    goto Done;
  }
  if (Info->FileSize == 0 || Info->FileSize > MAX_UINTN) {
    Status = EFI_BAD_BUFFER_SIZE;
    goto Done;
  }

  Bytes = (UINTN)Info->FileSize;
  Buffer = AllocatePool (Bytes);
  if (Buffer == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }

  Status = File->Read (File, &Bytes, Buffer);
  if (EFI_ERROR (Status)) {
    goto Done;
  }
  if (Bytes != (UINTN)Info->FileSize) {
    /* A short read on a payload is a corrupt payload, not a smaller one. */
    Status = EFI_DEVICE_ERROR;
    goto Done;
  }

  *Data = Buffer;
  *Size = Bytes;
  Buffer = NULL;
  Status = EFI_SUCCESS;

Done:
  if (Buffer != NULL) {
    FreePool (Buffer);
  }
  if (Info != NULL) {
    FreePool (Info);
  }
  File->Close (File);
  Root->Close (Root);
  return Status;
}

/*
 * The standard LoadFile two-call contract: a NULL buffer or one that is too
 * small reports the required size, anything else copies. BootPolicy is FALSE
 * for an initrd - a TRUE call means "boot from this", which this handle is
 * not - and a non-end remainder means the caller asked for something inside a
 * path that has no inside.
 */
STATIC
EFI_STATUS
EFIAPI
SfbInitrdLoadFile (IN EFI_LOAD_FILE2_PROTOCOL   *This,
                   IN EFI_DEVICE_PATH_PROTOCOL  *FilePath,
                   IN BOOLEAN                   BootPolicy,
                   IN OUT UINTN                 *BufferSize,
                   OUT VOID                     *Buffer OPTIONAL)
{
  (VOID)This;

  if (BufferSize == NULL || FilePath == NULL || BootPolicy) {
    return EFI_INVALID_PARAMETER;
  }
  if (!IsDevicePathEnd (FilePath)) {
    return EFI_INVALID_PARAMETER;
  }
  if (mSfbInitrdData == NULL || mSfbInitrdSize == 0) {
    return EFI_NOT_FOUND;
  }
  if (Buffer == NULL || *BufferSize < mSfbInitrdSize) {
    *BufferSize = mSfbInitrdSize;
    return EFI_BUFFER_TOO_SMALL;
  }

  CopyMem (Buffer, mSfbInitrdData, mSfbInitrdSize);
  *BufferSize = mSfbInitrdSize;
  return EFI_SUCCESS;
}

EFI_STATUS
SfbInitrdInstall (IN EFI_HANDLE Volume, IN CONST CHAR16 *Path)
{
  EFI_STATUS  Status;

  if (Volume == NULL || Path == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (mSfbInitrdHandle != NULL) {
    return EFI_UNSUPPORTED;
  }

  Status = SfbLinuxReadWholeFile (Volume, Path, &mSfbInitrdData,
                                  &mSfbInitrdSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK initrd-read path='%s' status=%r\n",
            Path, Status));
    return Status;
  }

  ZeroMem (&mSfbInitrdPath, sizeof (mSfbInitrdPath));
  mSfbInitrdPath.Vendor.Header.Type = MEDIA_DEVICE_PATH;
  mSfbInitrdPath.Vendor.Header.SubType = MEDIA_VENDOR_DP;
  SetDevicePathNodeLength (&mSfbInitrdPath.Vendor.Header,
                           sizeof (mSfbInitrdPath.Vendor));
  CopyGuid (&mSfbInitrdPath.Vendor.Guid, &mSfbInitrdMediaGuid);
  SetDevicePathEndNode (&mSfbInitrdPath.End);

  mSfbInitrdLoadFile.LoadFile = SfbInitrdLoadFile;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mSfbInitrdHandle,
                  &gEfiDevicePathProtocolGuid, &mSfbInitrdPath,
                  &gEfiLoadFile2ProtocolGuid, &mSfbInitrdLoadFile,
                  NULL);
  if (EFI_ERROR (Status)) {
    mSfbInitrdHandle = NULL;
    FreePool (mSfbInitrdData);
    mSfbInitrdData = NULL;
    mSfbInitrdSize = 0;
    DEBUG ((EFI_D_ERROR, "SFB: MARK initrd-install status=%r\n", Status));
    return Status;
  }

  DEBUG ((EFI_D_INFO, "SFB: MARK initrd-install path='%s' bytes=%u\n",
          Path, (UINT32)mSfbInitrdSize));
  return EFI_SUCCESS;
}

VOID
SfbInitrdUninstall (VOID)
{
  if (mSfbInitrdHandle != NULL) {
    gBS->UninstallMultipleProtocolInterfaces (
           mSfbInitrdHandle,
           &gEfiDevicePathProtocolGuid, &mSfbInitrdPath,
           &gEfiLoadFile2ProtocolGuid, &mSfbInitrdLoadFile,
           NULL);
    mSfbInitrdHandle = NULL;
  }
  if (mSfbInitrdData != NULL) {
    FreePool (mSfbInitrdData);
    mSfbInitrdData = NULL;
  }
  mSfbInitrdSize = 0;
}

EFI_STATUS
SfbDtbInstall (IN EFI_HANDLE Volume, IN CONST CHAR16 *Path)
{
  EFI_STATUS            Status;
  VOID                  *File = NULL;
  UINTN                 Size = 0;
  UINT32                TotalSize;
  EFI_PHYSICAL_ADDRESS  Pages = 0;

  if (Volume == NULL || Path == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (mSfbDtb != NULL) {
    return EFI_UNSUPPORTED;
  }

  Status = SfbLinuxReadWholeFile (Volume, Path, &File, &Size);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK dtb-read path='%s' status=%r\n",
            Path, Status));
    return Status;
  }

  if (Size < 8 || SfbFdtBe32 ((CONST UINT8 *)File) != SFB_FDT_MAGIC) {
    FreePool (File);
    DEBUG ((EFI_D_ERROR, "SFB: MARK dtb-reject path='%s' reason=magic\n",
            Path));
    return EFI_UNSUPPORTED;
  }
  TotalSize = SfbFdtBe32 ((CONST UINT8 *)File + 4);
  if (TotalSize != (UINT32)Size) {
    /* A header that disagrees with the file is a truncated or padded DTB, and
     * the kernel walks it by the header's word, not by the file length. */
    FreePool (File);
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK dtb-reject path='%s' reason=size header=%u file=%u\n",
            Path, TotalSize, (UINT32)Size));
    return EFI_UNSUPPORTED;
  }

  /*
   * Pages, not pool: the kernel keeps reading this after ExitBootServices, and
   * only a memory type it is told to preserve survives that. EfiACPIReclaim is
   * what every other EFI loader uses for the DTB.
   */
  mSfbDtbPages = EFI_SIZE_TO_PAGES (Size);
  Status = gBS->AllocatePages (AllocateAnyPages, EfiACPIReclaimMemory,
                               mSfbDtbPages, &Pages);
  if (EFI_ERROR (Status)) {
    FreePool (File);
    mSfbDtbPages = 0;
    return Status;
  }
  mSfbDtb = (VOID *)(UINTN)Pages;
  CopyMem (mSfbDtb, File, Size);
  FreePool (File);

  Status = gBS->InstallConfigurationTable ((EFI_GUID *)&mSfbDtbTableGuid,
                                           mSfbDtb);
  if (EFI_ERROR (Status)) {
    gBS->FreePages (Pages, mSfbDtbPages);
    mSfbDtb = NULL;
    mSfbDtbPages = 0;
    DEBUG ((EFI_D_ERROR, "SFB: MARK dtb-install status=%r\n", Status));
    return Status;
  }

  DEBUG ((EFI_D_INFO, "SFB: MARK dtb-install path='%s' bytes=%u\n",
          Path, (UINT32)Size));
  return EFI_SUCCESS;
}

VOID
SfbDtbUninstall (VOID)
{
  if (mSfbDtb == NULL) {
    return;
  }
  gBS->InstallConfigurationTable ((EFI_GUID *)&mSfbDtbTableGuid, NULL);
  gBS->FreePages ((EFI_PHYSICAL_ADDRESS)(UINTN)mSfbDtb, mSfbDtbPages);
  mSfbDtb = NULL;
  mSfbDtbPages = 0;
}
