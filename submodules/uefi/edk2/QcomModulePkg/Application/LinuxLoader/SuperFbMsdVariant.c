/*
 * Bundled export drivers are started on demand. Manual storage enumerates as
 * class 08, 1209:ca0e; managed storage uses class FF, 1209:ca0f and its scoped
 * WinUSB descriptors. INQUIRY strings are "canoe" / "efisp boot root".
 * The removable bit follows the assigned LUN: the contained FAT image is
 * removable, while a physical persist partition retains fixed-media identity.
 *
 * The platform's own driver is never touched: the bundled image is loaded
 * into fresh pool memory allocated for us. Manual export may fall back to the
 * platform driver if its bundled variant cannot start. Managed export must
 * refuse that failure rather than fall back to an OS-owned storage identity.
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

/*
 * The bundled image, baked in at build time by
 * submodules/uefi/embed_variant.py. NULL with size 0 permits manual platform
 * fallback; managed export remains unavailable without its bundled variant.
 */
extern CONST UINT8  *gCanoeMsdVariant;
extern CONST UINTN  gCanoeMsdVariantSize;
extern CONST UINT8 *gCanoeManagedMsdVariant;
extern CONST UINTN gCanoeManagedMsdVariantSize;

STATIC CONST EFI_GUID mSfbMsdProtocolGuid = {
  0xc8591faf, 0xdbcc, 0x479e,
  { 0x9e, 0xf2, 0xfd, 0x08, 0x5b, 0xc3, 0x7b, 0xc7 }
};

typedef enum {
  SfbVariantUntried = 0,
  SfbVariantReady,
  SfbVariantFailed
} SFB_VARIANT_STATE;

typedef struct {
  SFB_USB_MSD_PROTOCOL *Protocol;
  SFB_VARIANT_STATE State;
  EFI_HANDLE Image;
} SFB_VARIANT;
STATIC SFB_VARIANT mVariants[2];

/*
 * Start the bundled driver and hand back its protocol instance. It installs
 * the same protocol GUID the platform driver does, so the instance is
 * identified by handle diff: snapshot the handles carrying the GUID, start
 * the image, and take the handle that appears. Tried once per boot; a
 * failure latches so later exports do not repeat the load attempt. The caller
 * chooses manual platform fallback or managed-export refusal.
 */
STATIC SFB_USB_MSD_PROTOCOL *
VariantProtocol (BOOLEAN Managed)
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Before = NULL;
  UINTN       BeforeCount = 0;
  EFI_HANDLE  *After = NULL;
  UINTN       AfterCount = 0;
  UINTN       Outer;
  UINTN       Inner;
  BOOLEAN     Known;
  EFI_HANDLE  NewHandle = NULL;
  VOID        *Protocol = NULL;
  SFB_VARIANT *Variant = &mVariants[Managed ? 1 : 0];
  CONST UINT8 *Blob = Managed ? gCanoeManagedMsdVariant : gCanoeMsdVariant;
  UINTN BlobSize = Managed ? gCanoeManagedMsdVariantSize : gCanoeMsdVariantSize;

  if (Variant->State == SfbVariantReady) {
    return Variant->Protocol;
  }
  if (Variant->State == SfbVariantFailed) {
    return NULL;
  }
  Variant->State = SfbVariantFailed;
  Status = EFI_NOT_FOUND;

  if (Blob == NULL || BlobSize == 0) {
    goto Out;
  }

  (VOID)gBS->LocateHandleBuffer (ByProtocol, (EFI_GUID *)&mSfbMsdProtocolGuid,
                                 NULL, &BeforeCount, &Before);

  /* A crash inside LoadImage/StartImage must be attributable from the marks
   * alone, so the pre-call marks land before either runs. The magic check
   * keeps a corrupt embed from becoming a wild jump. The load uses the PI
   * memory form (no device path, source buffer set), which predates and
   * outlives the MEMMAP-path form across vendor DXE cores. */
  if (BlobSize < 0x40 ||
      Blob[0] != 'M' || Blob[1] != 'Z') {
    Status = EFI_LOAD_ERROR;
    DEBUG ((EFI_D_ERROR, "SFB: MARK msc-dxe status=%r reason=bad-embed\n",
            Status));
    goto Out;
  }
  DEBUG ((EFI_D_ERROR,
          "SFB: MARK msc-dxe stage=load-entered size=0x%Lx status=%r\n",
          (UINT64)BlobSize, EFI_NOT_STARTED));

  Status = gBS->LoadImage (FALSE, gImageHandle, NULL, (VOID *)Blob,
                           BlobSize, &Variant->Image);
  DEBUG ((EFI_D_ERROR, "SFB: MARK msc-dxe load status=%r\n", Status));
  if (EFI_ERROR (Status)) {
    goto Out;
  }
  DEBUG ((EFI_D_ERROR,
          "SFB: MARK msc-dxe stage=start-entered status=%r\n",
          EFI_NOT_STARTED));
  Status = gBS->StartImage (Variant->Image, 0, NULL);
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
    Status = EFI_NOT_FOUND;
    DEBUG ((EFI_D_ERROR, "SFB: MARK msc-dxe status=%r reason=no-new-handle\n",
            Status));
    goto Out;
  }
  Status = gBS->HandleProtocol (NewHandle, (EFI_GUID *)&mSfbMsdProtocolGuid,
                                &Protocol);
  DEBUG ((EFI_D_ERROR, "SFB: MARK msc-dxe proto status=%r\n", Status));
  if (EFI_ERROR (Status) || Protocol == NULL) {
    if (!EFI_ERROR (Status)) {
      Status = EFI_NOT_FOUND;
    }
    goto Out;
  }
  Variant->Protocol      = (SFB_USB_MSD_PROTOCOL *)Protocol;
  Variant->State = SfbVariantReady;

Out:
  DEBUG (((Variant->Protocol != NULL) ? EFI_D_INFO : EFI_D_WARN,
          "SFB: MARK msc-dxe selected=%a usable=%u status=%r\n",
          (Variant->Protocol != NULL) ? "bundled" : "platform",
          (UINT32)(Variant->Protocol != NULL),
          (Variant->Protocol != NULL) ? EFI_SUCCESS : Status));
  if (Before != NULL) {
    FreePool (Before);
  }
  if (After != NULL) {
    FreePool (After);
  }
  return Variant->Protocol;
}

SFB_USB_MSD_PROTOCOL *SfbMsdVariantProtocol (VOID) { return VariantProtocol (FALSE); }
SFB_USB_MSD_PROTOCOL *SfbMsdManagedProtocol (VOID) { return VariantProtocol (TRUE); }
BOOLEAN SfbMsdManagedAvailable (VOID)
{
  return gCanoeManagedMsdVariant != NULL && gCanoeManagedMsdVariantSize >= 0x40;
}
