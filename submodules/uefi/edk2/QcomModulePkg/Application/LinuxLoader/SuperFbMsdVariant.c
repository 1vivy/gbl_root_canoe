/*
 * Resident UsbMsdDxe variant work: locate the loaded driver image, patch its
 * mass-storage presentation to the canoe identity, and export the image
 * itself as a RAM-disk LUN for calibration.
 *
 * Why this exists: the platform's resident UsbMsdDxe reports every export as
 * VID:PID 05c6:f000 with the SCSI removable-media bit set. That identity is
 * shared with mode-switching 4G modems, so stock Linux desktops tear the
 * session down on sight (usb_modeswitch), and "removable" invites host eject
 * behaviour the session cannot survive. The driver is a signed-firmware
 * blob, so the variant is produced at runtime: before StartDevice, the
 * loaded image's INQUIRY RMB instruction and its two device descriptors are
 * rewritten in memory. The BOT/SCSI engine underneath is untouched.
 *
 * The canoe identity (VID 0x1209, the pid.codes community range, product
 * string "efisp boot root") matches no modeswitch rule and no Qualcomm host
 * tool, so a patched export is discovered by identity alone on Linux,
 * Windows, and macOS with zero host-side ceremony. The unpatched identity
 * remains the fallback when a firmware build's layout defeats the scan.
 *
 * Patch anchors, all verified against the driver's own source
 * (BOOT.MXF.2.5.1 QcomPkg/Drivers/UsbMsdDxe) and the corpus of 46 patched
 * variants in Project-Silicium/Device-Binaries:
 *
 *  - RMB: in handle_inquiry the response byte is built by
 *    `mov w8, #0x80` next to `mov w9, #4; mov w10, #2; mov w11, #0x1f`.
 *    The upstream corpus patches the same word to `mov w8, #0`.
 *  - Descriptors: DeviceDescriptor and SSDeviceDescriptor are CONST tables
 *    with idVendor=0x05C6 idProduct=0xF000 (UsbMsdDesc.c); the muyu variant
 *    in the corpus patches the same two words.
 *  - Strings: the INQUIRY vendor/product literals sit concatenated in .data
 *    as "QualcommMMC Storage     " (8 + 16 fixed-width fields).
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbMassStorage.h"
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/LoadedImage.h>

/* The canoe presentation identity. 0x1209 is the pid.codes community VID;
 * 0xCA0E carries no registration and collides with nothing in the
 * usb_modeswitch database or the Qualcomm host tooling. */
#define SFB_MSD_CANOE_VID  0x1209
#define SFB_MSD_CANOE_PID  0xCA0E

#define SFB_MSD_STOCK_VID  0x05C6
#define SFB_MSD_STOCK_PID  0xF000

/* Fixed-width INQUIRY fields: 8 vendor + 16 product. */
#define SFB_MSD_STOCK_VENDOR   "Qualcomm"
#define SFB_MSD_CANOE_VENDOR   "canoe   "
#define SFB_MSD_STOCK_PRODUCT  "MMC Storage     "
#define SFB_MSD_CANOE_PRODUCT  "efisp boot root "

/*
 * The INQUIRY builder on the modern codegen: mov w8,#0x80 ; mov w9,#4 ;
 * mov w10,#2 ; mov w11,#0x1f. Only the first word is patched, exactly like
 * the upstream corpus: the immediate 0x80 becomes 0x00.
 */
STATIC CONST UINT8 mSfbMsdRmbPattern[] = {
  0x08, 0x10, 0x80, 0x52,   /* mov w8, #0x80  */
  0x89, 0x00, 0x80, 0x52,   /* mov w9, #4     */
  0x4A, 0x00, 0x80, 0x52,   /* mov w10, #2    */
  0xEB, 0x03, 0x80, 0x52    /* mov w11, #0x1f */
};
STATIC CONST UINT8 mSfbMsdRmbPatch[] = {
  0x08, 0x00, 0x80, 0x52    /* mov w8, #0     */
};

STATIC CONST EFI_GUID mSfbMsdProtocolGuid = {
  0xc8591faf, 0xdbcc, 0x479e,
  { 0x9e, 0xf2, 0xfd, 0x08, 0x5b, 0xc3, 0x7b, 0xc7 }
};

STATIC UINT64
SfbMsdFnv64 (IN CONST UINT8 *Data, IN UINTN Size)
{
  UINT64 Hash = 14695981039346656037ULL;
  UINTN Index;

  for (Index = 0; Index < Size; Index++) {
    Hash ^= Data[Index];
    Hash *= 1099511628211ULL;
  }
  return Hash;
}

/*
 * The resident driver registers its protocol on its own handle during the
 * XBL DXE phase, so the image that owns it is found by walking the handles
 * that carry the MSD protocol GUID and reading their LoadedImage.
 */
EFI_STATUS
SfbMsdLocateImage (OUT SFB_MSD_IMAGE *Image)
{
  EFI_STATUS                 Status;
  EFI_HANDLE                 *Handles = NULL;
  UINTN                      HandleCount = 0;
  UINTN                      Index;
  EFI_LOADED_IMAGE_PROTOCOL  *Loaded = NULL;

  if (Image == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Image->Base = NULL;
  Image->Size = 0;
  Image->Fnv  = 0;

  Status = gBS->LocateHandleBuffer (ByProtocol,
                                    (EFI_GUID *)&mSfbMsdProtocolGuid, NULL,
                                    &HandleCount, &Handles);
  if (EFI_ERROR (Status) || Handles == NULL) {
    return EFI_NOT_FOUND;
  }
  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (Handles[Index],
                                  &gEfiLoadedImageProtocolGuid,
                                  (VOID **)&Loaded);
    if (EFI_ERROR (Status) || Loaded == NULL ||
        Loaded->ImageBase == NULL || Loaded->ImageSize == 0) {
      continue;
    }
    Image->Base = Loaded->ImageBase;
    Image->Size = (UINTN)Loaded->ImageSize;
    break;
  }
  FreePool (Handles);
  if (Image->Base == NULL) {
    return EFI_NOT_FOUND;
  }
  Image->Fnv = SfbMsdFnv64 (Image->Base, Image->Size);
  DEBUG ((EFI_D_ERROR, "SFB: MARK msd-image base=0x%Lx size=0x%Lx fnv=0x%Lx\n",
          (UINT64)(UINTN)Image->Base, (UINT64)Image->Size, Image->Fnv));
  return EFI_SUCCESS;
}

/*
 * Bounded scan for NEEDLE inside HAY. Returns the hit offset or MAX_UINTN.
 * Only the first hit matters for every anchor here: the patterns are either
 * unique by construction (the instruction quadruple) or validated at each
 * hit (the descriptor header two bytes before the VID word).
 */
STATIC UINTN
SfbMsdFind (IN CONST UINT8 *Hay, IN UINTN HaySize,
            IN CONST UINT8 *Needle, IN UINTN NeedleSize,
            IN UINTN From)
{
  UINTN Index;

  if (NeedleSize == 0 || HaySize < NeedleSize || From > HaySize - NeedleSize) {
    return MAX_UINTN;
  }
  for (Index = From; Index + NeedleSize <= HaySize; Index++) {
    if (CompareMem (Hay + Index, Needle, NeedleSize) == 0) {
      return Index;
    }
  }
  return MAX_UINTN;
}

STATIC UINT32
SfbMsdPatchRmb (IN OUT UINT8 *Image, IN UINTN Size)
{
  UINTN Off;

  Off = SfbMsdFind (Image, Size, mSfbMsdRmbPattern, sizeof (mSfbMsdRmbPattern),
                    0);
  if (Off == MAX_UINTN) {
    return 0;
  }
  if (Image[Off + 1] == 0x00) {
    return 2;   /* already the canoe form */
  }
  CopyMem (Image + Off, mSfbMsdRmbPatch, sizeof (mSfbMsdRmbPatch));
  return (Image[Off] == mSfbMsdRmbPatch[0] && Image[Off + 1] == 0x00) ? 1 : 0;
}

STATIC UINT32
SfbMsdPatchDescriptors (IN OUT UINT8 *Image, IN UINTN Size)
{
  STATIC CONST UINT8 Stock[] = {
    (UINT8)(SFB_MSD_STOCK_VID & 0xFF), (UINT8)(SFB_MSD_STOCK_VID >> 8),
    (UINT8)(SFB_MSD_STOCK_PID & 0xFF), (UINT8)(SFB_MSD_STOCK_PID >> 8)
  };
  STATIC CONST UINT8 Canoe[] = {
    (UINT8)(SFB_MSD_CANOE_VID & 0xFF), (UINT8)(SFB_MSD_CANOE_VID >> 8),
    (UINT8)(SFB_MSD_CANOE_PID & 0xFF), (UINT8)(SFB_MSD_CANOE_PID >> 8)
  };
  UINTN  From = 0;
  UINTN  Off;
  UINT32 Patched = 0;
  UINT32 Already = 0;

  while (TRUE) {
    Off = SfbMsdFind (Image, Size, Stock, sizeof (Stock), From);
    if (Off == MAX_UINTN) {
      break;
    }
    From = Off + sizeof (Stock);
    /* A device descriptor is 0x12 0x01 ... <bcdUSB> ... with the VID word at
     * offset 8. Requiring the header keeps random data matches out. */
    if (Off < 8 || Image[Off - 8] != 0x12 || Image[Off - 7] != 0x01) {
      continue;
    }
    CopyMem (Image + Off, Canoe, sizeof (Canoe));
    if (CompareMem (Image + Off, Canoe, sizeof (Canoe)) == 0) {
      Patched++;
    }
  }

  From = 0;
  while (TRUE) {
    Off = SfbMsdFind (Image, Size, Canoe, sizeof (Canoe), From);
    if (Off == MAX_UINTN) {
      break;
    }
    From = Off + sizeof (Canoe);
    Already++;
  }
  if (Patched == 0 && Already > 0) {
    return 2;   /* already the canoe form from an earlier session */
  }
  return (Patched > 0) ? 1 : 0;
}

STATIC UINT32
SfbMsdPatchStrings (IN OUT UINT8 *Image, IN UINTN Size)
{
  STATIC CONST CHAR8 Stock[] =
    SFB_MSD_STOCK_VENDOR SFB_MSD_STOCK_PRODUCT;   /* 24 bytes, no NUL */
  STATIC CONST CHAR8 Canoe[] =
    SFB_MSD_CANOE_VENDOR SFB_MSD_CANOE_PRODUCT;   /* 24 bytes, no NUL */
  UINTN Off;

  Off = SfbMsdFind (Image, Size, (CONST UINT8 *)Stock, 24, 0);
  if (Off == MAX_UINTN) {
    Off = SfbMsdFind (Image, Size, (CONST UINT8 *)Canoe, 24, 0);
    return (Off == MAX_UINTN) ? 0 : 2;
  }
  CopyMem (Image + Off, Canoe, 24);
  return (CompareMem (Image + Off, Canoe, 24) == 0) ? 1 : 0;
}

/*
 * Produce the canoe variant of the resident driver in memory. Idempotent:
 * each anchor reports patched(1)/already(2)/miss(0), so a second export in
 * the same boot is a no-op and an unfamiliar firmware layout degrades to the
 * stock presentation instead of failing the export.
 */
VOID
SfbMsdApplyVariant (VOID)
{
  EFI_STATUS      Status;
  SFB_MSD_IMAGE Image;
  UINT32          Rmb;
  UINT32          Desc;
  UINT32          Str;

  Status = SfbMsdLocateImage (&Image);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK msc-variant status=%r reason=no-image\n", Status));
    return;
  }

  Rmb  = SfbMsdPatchRmb (Image.Base, Image.Size);
  Desc = SfbMsdPatchDescriptors (Image.Base, Image.Size);
  Str  = SfbMsdPatchStrings (Image.Base, Image.Size);

  DEBUG ((EFI_D_ERROR,
          "SFB: MARK msc-variant fnv=0x%Lx rmb=%u desc=%u str=%u\n",
          Image.Fnv, Rmb, Desc, Str));
}

/*
 * Read-only RAM disk around a pool copy of the loaded image, so the dump
 * runs through the exact same LUN/export path as a partition and needs no
 * filesystem write support on the device at all.
 */
typedef struct {
  EFI_BLOCK_IO_PROTOCOL  BlockIo;
  EFI_BLOCK_IO_MEDIA     Media;
  UINT8                  *Data;
  UINTN                  Bytes;
} SFB_MSD_RAMDISK;

STATIC
EFI_STATUS
EFIAPI
SfbMsdRamRead (IN EFI_BLOCK_IO_PROTOCOL *This,
               IN UINT32                MediaId,
               IN EFI_LBA               Lba,
               IN UINTN                 BufferSize,
               OUT VOID                 *Buffer)
{
  SFB_MSD_RAMDISK *Disk = (SFB_MSD_RAMDISK *)This;
  UINT64          Start;
  UINT64          End;

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (MediaId != This->Media->MediaId) {
    return EFI_MEDIA_CHANGED;
  }
  Start = (UINT64)Lba * This->Media->BlockSize;
  End   = Start + BufferSize;
  if (End > Disk->Bytes || End < Start) {
    return EFI_INVALID_PARAMETER;
  }
  if (BufferSize != 0) {
    CopyMem (Buffer, Disk->Data + Start, BufferSize);
  }
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SfbMsdRamWrite (IN EFI_BLOCK_IO_PROTOCOL *This,
                IN UINT32                MediaId,
                IN EFI_LBA               Lba,
                IN UINTN                 BufferSize,
                IN VOID                  *Buffer)
{
  return EFI_WRITE_PROTECTED;
}

STATIC
EFI_STATUS
EFIAPI
SfbMsdRamFlush (IN EFI_BLOCK_IO_PROTOCOL *This)
{
  return EFI_SUCCESS;
}

EFI_STATUS
SfbExportMsdImage (VOID)
{
  EFI_STATUS      Status;
  SFB_MSD_IMAGE   Image;
  SFB_MSD_RAMDISK *Disk;
  UINTN           Padded;

  Status = SfbMsdLocateImage (&Image);
  if (EFI_ERROR (Status)) {
    SfbUsbCensus ();
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK msc-run target=msdimage status=%r reason=no-image\n",
            Status));
    return Status;
  }

  Padded = (Image.Size + 511) & ~(UINTN)511;
  Disk = AllocateZeroPool (sizeof (SFB_MSD_RAMDISK));
  if (Disk == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  Disk->Data = AllocatePool (Padded);
  if (Disk->Data == NULL) {
    FreePool (Disk);
    return EFI_OUT_OF_RESOURCES;
  }
  CopyMem (Disk->Data, Image.Base, Image.Size);
  Disk->Bytes = Padded;

  Disk->BlockIo.Revision  = EFI_BLOCK_IO_PROTOCOL_REVISION;
  Disk->BlockIo.Media     = &Disk->Media;
  Disk->BlockIo.Reset     = NULL;
  Disk->BlockIo.ReadBlocks  = SfbMsdRamRead;
  Disk->BlockIo.WriteBlocks = SfbMsdRamWrite;
  Disk->BlockIo.FlushBlocks = SfbMsdRamFlush;
  Disk->Media.MediaId          = 1;
  Disk->Media.RemovableMedia   = FALSE;
  Disk->Media.MediaPresent     = TRUE;
  Disk->Media.LogicalPartition = FALSE;
  Disk->Media.ReadOnly         = TRUE;
  Disk->Media.WriteCaching     = FALSE;
  Disk->Media.BlockSize        = 512;
  Disk->Media.IoAlign          = 1;
  Disk->Media.LastBlock        = (Padded / 512) - 1;

  Status = SfbMassStorageExportDisk (&Disk->BlockIo, "msdimage");

  FreePool (Disk->Data);
  FreePool (Disk);
  return Status;
}
