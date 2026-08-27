/*
 * The bundled canoe UsbMsd variant: a byte-patched copy of the platform's
 * closed-source UsbMsdDxe, built offline in the canoe-msd project, carried
 * in this image and started on demand.
 *
 * Why bundled instead of patched in place: writing the resident driver's
 * loaded image at runtime risks a data abort that the oplus XBL turns into
 * a QUSB_BULK dump-mode reset (observed on the OnePlus 15). Loading our own
 * instance writes only fresh pool memory allocated for us; the resident
 * driver stays byte-identical, and every failure mode degrades to it.
 *
 * The variant presents the export as VID:PID 1209:ca0e (pid.codes community
 * VID) with INQUIRY strings "canoe" / "efisp boot root" and the removable
 * bit clear: no usb_modeswitch rule or Qualcomm host tool claims the
 * identity, so enumeration needs zero host ceremony on Linux, Windows and
 * macOS. The resident driver (05c6:f000, removable) remains the fallback
 * when the variant is absent or cannot start.
 *
 * This file also owns the calibration instrument: SfbExportMsdImage serves
 * a copy of the resident driver's loaded image as a read-only RAM-disk LUN,
 * which is how a new target hands its exact blob to the host without any
 * filesystem write support on the device.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbMassStorage.h"
#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DevicePathLib.h>
#include <Protocol/LoadedImage.h>

/* Generated at build time by submodules/uefi/embed_variant.py from the
 * canoe-msd submodule's blob; size 0 means this build carries no variant
 * and every export falls back to the resident driver. */
extern UINT8 gCanoeMsdVariant[];
extern UINTN gCanoeMsdVariantSize;

STATIC CONST EFI_GUID mSfbMsdProtocolGuid = {
  0xc8591faf, 0xdbcc, 0x479e,
  { 0x9e, 0xf2, 0xfd, 0x08, 0x5b, 0xc3, 0x7b, 0xc7 }
};

/*
 * Generation markers, read (never written) out of the target's own resident
 * driver to decide whether the bundled variant belongs on this platform.
 *
 * The published corpus of 72 UsbMsdDxe blobs (Project-Silicium
 * Device-Binaries, surveyed by canoe-msd tools/corpus_survey.py) holds 64
 * distinct binaries in 61 distinct layouts and splits exactly in half: 36
 * carry this INQUIRY builder quadruple next to a 05c6:f000 device
 * descriptor - the codegen the bundled variant is built from, covering the
 * recent generations including infiniti - and 36 older ones do not. A
 * target that does not show both markers is a different driver generation,
 * so the variant stays unloaded and the resident driver serves the export
 * with its stock presentation.
 */
STATIC CONST UINT8 mSfbMsdRmbPattern[] = {
  0x08, 0x10, 0x80, 0x52,   /* mov w8, #0x80  */
  0x89, 0x00, 0x80, 0x52,   /* mov w9, #4     */
  0x4A, 0x00, 0x80, 0x52,   /* mov w10, #2    */
  0xEB, 0x03, 0x80, 0x52    /* mov w11, #0x1f */
};
STATIC CONST UINT8 mSfbMsdStockVidPid[] = { 0xC6, 0x05, 0x00, 0xF0 };

/* Substring search; with RequireDescriptorHeader the hit must sit at
 * offset 8 of an 0x12 0x01 device descriptor, which keeps random data
 * matches out of the generation decision. */
STATIC BOOLEAN
SfbMsdFindBytes (IN CONST UINT8 *Hay, IN UINTN HaySize,
                 IN CONST UINT8 *Needle, IN UINTN NeedleSize,
                 IN BOOLEAN RequireDescriptorHeader)
{
  UINTN Index;

  if (NeedleSize == 0 || HaySize < NeedleSize) {
    return FALSE;
  }
  for (Index = 0; Index + NeedleSize <= HaySize; Index++) {
    if (CompareMem (Hay + Index, Needle, NeedleSize) != 0) {
      continue;
    }
    if (!RequireDescriptorHeader) {
      return TRUE;
    }
    if (Index >= 8 && Hay[Index - 8] == 0x12 && Hay[Index - 7] == 0x01) {
      return TRUE;
    }
  }
  return FALSE;
}

typedef enum {
  SfbVariantUntried = 0,
  SfbVariantReady,
  SfbVariantFailed
} SFB_VARIANT_STATE;

STATIC SFB_USB_MSD_PROTOCOL *mSfbVariant = NULL;
STATIC SFB_VARIANT_STATE     mSfbVariantState = SfbVariantUntried;
STATIC EFI_HANDLE            mSfbVariantImage = NULL;

STATIC UINT64
SfbMsdFnv64 (IN CONST UINT8 *Data, IN UINTN Size)
{
  UINT64 Hash = 14695981039346656037ULL;
  UINTN  Index;

  for (Index = 0; Index < Size; Index++) {
    Hash ^= Data[Index];
    Hash *= 1099511628211ULL;
  }
  return Hash;
}

/*
 * The resident driver registers its protocol on its own handle during the
 * XBL DXE phase, so the image that owns it is found by walking the handles
 * that carry the MSD protocol GUID and reading their LoadedImage. Once the
 * bundled variant is started it carries the same GUID, so its own image
 * handle is skipped: the calibration dump and the generation check both
 * mean the platform's driver, never our copy of one.
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
    if (mSfbVariantImage != NULL && Handles[Index] == mSfbVariantImage) {
      continue;
    }
    Status = gBS->HandleProtocol (Handles[Index],
                                  &gEfiLoadedImageProtocolGuid,
                                  (VOID **)&Loaded);
    if (EFI_ERROR (Status) || Loaded == NULL ||
        Loaded->ImageBase == NULL || Loaded->ImageSize == 0) {
      continue;
    }
    if (Loaded->ImageBase == (VOID *)gCanoeMsdVariant) {
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
 * Is the bundled variant the right generation for this platform? Decided
 * from the target's own resident driver, read-only: both the INQUIRY
 * builder quadruple and a stock 05c6:f000 device descriptor must be
 * present, which is exactly the half of the published corpus the variant
 * was built from. Anything else keeps the resident driver.
 */
STATIC BOOLEAN
SfbMsdVariantFitsTarget (VOID)
{
  SFB_MSD_IMAGE Image;
  BOOLEAN       Rmb;
  BOOLEAN       Desc;

  if (EFI_ERROR (SfbMsdLocateImage (&Image))) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK msc-dxe compat=0 reason=no-resident\n"));
    return FALSE;
  }
  Rmb = SfbMsdFindBytes (Image.Base, Image.Size, mSfbMsdRmbPattern,
                         sizeof (mSfbMsdRmbPattern), FALSE);
  Desc = SfbMsdFindBytes (Image.Base, Image.Size, mSfbMsdStockVidPid,
                          sizeof (mSfbMsdStockVidPid), TRUE);
  DEBUG ((EFI_D_ERROR, "SFB: MARK msc-dxe compat=%u rmb=%u desc=%u\n",
          (UINT32)(Rmb && Desc), (UINT32)Rmb, (UINT32)Desc));
  return (BOOLEAN)(Rmb && Desc);
}

/*
 * Start the bundled variant and hand back its protocol instance. The
 * variant and the resident driver install the same protocol GUID, so the
 * instance is identified by handle diff: snapshot the handles carrying the
 * GUID, start the image, and take the handle that appears. Tried once per
 * boot; a failure latches so the export falls back to the resident driver
 * without re-paying the load.
 */
SFB_USB_MSD_PROTOCOL *
SfbMsdVariantProtocol (VOID)
{
  EFI_STATUS Status;
  EFI_HANDLE *Before = NULL;
  UINTN      BeforeCount = 0;
  EFI_HANDLE *After = NULL;
  UINTN      AfterCount = 0;
  UINTN      Outer;
  UINTN      Inner;
  BOOLEAN    Known;
  EFI_HANDLE NewHandle = NULL;
  VOID       *Protocol = NULL;

  if (mSfbVariantState == SfbVariantReady) {
    return mSfbVariant;
  }
  if (mSfbVariantState == SfbVariantFailed ||
      gCanoeMsdVariantSize == 0) {
    return NULL;
  }
  mSfbVariantState = SfbVariantFailed;

  /* Wrong driver generation on this target: keep the resident driver
   * rather than run a foreign build against its USB stack. */
  if (!SfbMsdVariantFitsTarget ()) {
    return NULL;
  }

  (VOID)gBS->LocateHandleBuffer (ByProtocol, (EFI_GUID *)&mSfbMsdProtocolGuid,
                                 NULL, &BeforeCount, &Before);

  /* A crash inside LoadImage/StartImage must be attributable from the marks
   * alone, so the pre-call marks land before either runs. The magic check
   * keeps a corrupt embed from becoming a wild jump. The load uses the PI
   * memory form (no device path, source buffer set), which predates and
   * outlives the MEMMAP-path form across vendor DXE cores. */
  if (gCanoeMsdVariantSize < 0x40 ||
      gCanoeMsdVariant[0] != 'M' || gCanoeMsdVariant[1] != 'Z') {
    DEBUG ((EFI_D_ERROR, "SFB: MARK msc-dxe status=%r reason=bad-embed\n",
            EFI_LOAD_ERROR));
    goto Out;
  }
  DEBUG ((EFI_D_ERROR, "SFB: MARK msc-dxe step=pre-load size=0x%Lx\n",
          (UINT64)gCanoeMsdVariantSize));

  Status = gBS->LoadImage (FALSE, gImageHandle, NULL, gCanoeMsdVariant,
                           (UINTN)gCanoeMsdVariantSize, &mSfbVariantImage);
  DEBUG ((EFI_D_ERROR, "SFB: MARK msc-dxe load status=%r size=0x%Lx\n",
          Status, (UINT64)gCanoeMsdVariantSize));
  if (EFI_ERROR (Status)) {
    goto Out;
  }
  DEBUG ((EFI_D_ERROR, "SFB: MARK msc-dxe step=pre-start\n"));
  Status = gBS->StartImage (mSfbVariantImage, 0, NULL);
  DEBUG ((EFI_D_ERROR, "SFB: MARK msc-dxe start status=%r\n", Status));
  if (EFI_ERROR (Status)) {
    goto Out;
  }

  Status = gBS->LocateHandleBuffer (ByProtocol, (EFI_GUID *)&mSfbMsdProtocolGuid,
                                    NULL, &AfterCount, &After);
  if (EFI_ERROR (Status)) {
    goto Out;
  }
  for (Outer = 0; Outer < AfterCount; Outer++) {
    Known = FALSE;
    for (Inner = 0; Inner < BeforeCount; Inner++) {
      if (After[Outer] == Before[Inner]) {
        Known = TRUE;
        break;
      }
    }
    if (!Known) {
      NewHandle = After[Outer];
      break;
    }
  }
  if (NewHandle == NULL) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK msc-dxe status=%r reason=no-new-handle\n",
            EFI_NOT_FOUND));
    goto Out;
  }
  Status = gBS->HandleProtocol (NewHandle, (EFI_GUID *)&mSfbMsdProtocolGuid,
                                &Protocol);
  DEBUG ((EFI_D_ERROR, "SFB: MARK msc-dxe proto status=%r\n", Status));
  if (EFI_ERROR (Status) || Protocol == NULL) {
    goto Out;
  }

  mSfbVariant      = (SFB_USB_MSD_PROTOCOL *)Protocol;
  mSfbVariantState = SfbVariantReady;

Out:
  if (Before != NULL) {
    FreePool (Before);
  }
  if (After != NULL) {
    FreePool (After);
  }
  return mSfbVariant;
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
