/*
 * USB mass-storage export over the platform's EFI_USB_MSD_PROTOCOL.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SUPER_FB_MSC_H__
#define __SUPER_FB_MSC_H__

#include <Uefi.h>
#include <Protocol/BlockIo.h>

/*
 * Export one disk to the connected PC as USB mass-storage LUN 0. Returns
 * EFI_ABORTED when the operator stopped the session with Volume Down,
 * EFI_SUCCESS when the host side ended it (unplug or link loss), or the
 * platform error if the session could not be started at all.
 */
EFI_STATUS
SfbMscExportDisk (IN EFI_BLOCK_IO_PROTOCOL *BlockIo, IN CONST CHAR8 *Tag);

VOID
SfbUsbCensus (VOID);

#endif
