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

#endif
