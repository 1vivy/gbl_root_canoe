/*
 * USB mass-storage export over the platform's EFI_USB_MSD_PROTOCOL.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SUPER_FB_MASS_STORAGE_H__
#define __SUPER_FB_MASS_STORAGE_H__

#include <Uefi.h>
#include <Protocol/BlockIo.h>

/*
 * Export one disk to the connected PC as USB mass-storage LUN 0.
 *
 * Returns EFI_ABORTED when the operator stopped the session with Volume Down,
 * which is the ordinary ending: an unplug or link loss no longer ends it, so
 * replugging resumes the same session rather than needing a new one.
 * EFI_SUCCESS means the vendor stack answered nothing for a sustained run and
 * the session gave up on its own. Anything else is the platform error from a
 * session that could not be started at all.
 */
EFI_STATUS
SfbMassStorageExportDisk (IN EFI_BLOCK_IO_PROTOCOL *BlockIo,
                          IN CONST CHAR8           *Tag);

VOID
SfbUsbCensus (VOID);

/*
 * Where the resident UsbMsdDxe lives once its protocol is installed: the
 * loaded image's base, size and an FNV-1a identity over its bytes. The FNV
 * is a debug-build calibration key, not a cryptographic hash; per-firmware
 * calibration tables key off the full SHA-256 of the dumped bytes on the
 * host side instead.
 */
typedef struct {
  VOID   *Base;
  UINTN  Size;
  UINT64 Fnv;
} SFB_MSD_IMAGE;

EFI_STATUS
SfbMsdLocateImage (OUT SFB_MSD_IMAGE *Image);

/*
 * Rewrite the resident driver's mass-storage presentation to the canoe
 * identity (fixed-disk INQUIRY, VID 0x1209 PID 0xCA0E, canoe INQUIRY
 * strings) before the session starts. Idempotent and best-effort: an
 * unfamiliar firmware layout leaves the stock presentation, which keeps the
 * export working the way it always has.
 */
VOID
SfbMsdApplyVariant (VOID);

/*
 * Export a copy of the resident driver's loaded image as a read-only
 * RAM-disk LUN, so any target can hand its exact blob to the host for
 * calibration without any filesystem write support on the device.
 */
EFI_STATUS
SfbExportMsdImage (VOID);

#endif
