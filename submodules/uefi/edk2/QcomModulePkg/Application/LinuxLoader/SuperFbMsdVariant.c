/*
 * The bundled mass-storage driver: carried inside this image and started on
 * demand, so an export enumerates as 1209:ca0e with INQUIRY strings "canoe"
 * / "efisp boot root" and the removable bit clear. No stock host rule
 * claims that identity, so a session needs no host-side ceremony on Linux,
 * Windows or macOS.
 *
 * The platform's own driver is never touched: the bundled image is loaded
 * into fresh pool memory allocated for us. Every failure mode - no blob in
 * this build, refused load, refused start - degrades to the platform driver,
 * which exports under its own identity exactly as it did before.
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
 * submodules/uefi/embed_variant.py. NULL with size 0 is a valid build: every
 * export then uses the platform driver.
 */
extern CONST UINT8  *gCanoeMsdVariant;
extern CONST UINTN  gCanoeMsdVariantSize;

STATIC CONST EFI_GUID mSfbMsdProtocolGuid = {
  0xc8591faf, 0xdbcc, 0x479e,
  { 0x9e, 0xf2, 0xfd, 0x08, 0x5b, 0xc3, 0x7b, 0xc7 }
};

typedef enum {
  SfbVariantUntried = 0,
  SfbVariantReady,
  SfbVariantFailed
} SFB_VARIANT_STATE;

STATIC SFB_USB_MSD_PROTOCOL *mSfbVariant = NULL;
STATIC SFB_VARIANT_STATE     mSfbVariantState = SfbVariantUntried;
STATIC EFI_HANDLE            mSfbVariantImage = NULL;

/*
 * Start the bundled driver and hand back its protocol instance. It installs
 * the same protocol GUID the platform driver does, so the instance is
 * identified by handle diff: snapshot the handles carrying the GUID, start
 * the image, and take the handle that appears. Tried once per boot; a
 * failure latches so later exports fall back without re-paying the load.
 */
SFB_USB_MSD_PROTOCOL *
SfbMsdVariantProtocol (VOID)
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

  if (mSfbVariantState == SfbVariantReady) {
    return mSfbVariant;
  }
  if (mSfbVariantState == SfbVariantFailed) {
    return NULL;
  }
  mSfbVariantState = SfbVariantFailed;

  if (gCanoeMsdVariant == NULL || gCanoeMsdVariantSize == 0) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK msc-dxe match=0 reason=none-bundled\n"));
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

  Status = gBS->LoadImage (FALSE, gImageHandle, NULL, (VOID *)gCanoeMsdVariant,
                           gCanoeMsdVariantSize, &mSfbVariantImage);
  DEBUG ((EFI_D_ERROR, "SFB: MARK msc-dxe load status=%r\n", Status));
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
