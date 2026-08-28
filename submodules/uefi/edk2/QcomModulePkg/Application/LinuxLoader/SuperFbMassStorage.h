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

/* EFI_USB_MSD_PROTOCOL, mirrored from the public Mu-Silicium EFIUsbMsd.h and
 * verified field-for-field against the driver's own EFIUsbMsdPeripheral.h
 * (revision 0x00010003). Shared by the resident instance and the bundled
 * canoe variant DXE, which implements the same interface. */
typedef struct _SFB_USB_MSD_PROTOCOL SFB_USB_MSD_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *SFB_USB_MSD_ASSIGN_BLK_IO) (
  IN SFB_USB_MSD_PROTOCOL  *This,
  IN EFI_BLOCK_IO_PROTOCOL *BlkIo,
  IN UINT32                Lun
  );

typedef
EFI_STATUS
(EFIAPI *SFB_USB_MSD_QUERY_MAX_LUN) (
  IN SFB_USB_MSD_PROTOCOL *This,
  OUT UINT8               *Count
  );

typedef
EFI_STATUS
(EFIAPI *SFB_USB_MSD_EVENT_HANDLER) (IN SFB_USB_MSD_PROTOCOL *This);

typedef
EFI_STATUS
(EFIAPI *SFB_USB_MSD_START_DEVICE) (IN SFB_USB_MSD_PROTOCOL *This);

typedef
EFI_STATUS
(EFIAPI *SFB_USB_MSD_STOP_DEVICE) (IN SFB_USB_MSD_PROTOCOL *This);

struct _SFB_USB_MSD_PROTOCOL {
  UINT32                     Revision;
  SFB_USB_MSD_ASSIGN_BLK_IO  AssignBlkIoHandle;
  SFB_USB_MSD_QUERY_MAX_LUN  QueryMaxLun;
  SFB_USB_MSD_EVENT_HANDLER  EventHandler;
  SFB_USB_MSD_START_DEVICE   StartDevice;
  SFB_USB_MSD_STOP_DEVICE    StopDevice;
  VOID                       *GetDeviceSpeed;
  VOID                       *UnmountHandle;
  VOID                       *MountHandle;
  VOID                       *FindPartitions;
};

/*
 * Export one disk to the connected PC as USB mass-storage LUN 0. Name is the
 * GPT partition name ("persist", "logfs"); the Block I/O is resolved here
 * rather than by the caller, and deliberately so. Starting an export first
 * releases USB host mode, which disconnects controllers and destroys their
 * child partition handles, so any EFI_BLOCK_IO_PROTOCOL obtained before that
 * point is dangling by the time it would be used. Taking the name makes the
 * stale pointer unrepresentable instead of merely unlikely.
 *
 * Returns EFI_ABORTED when the operator stopped the session with Volume Down,
 * which is the ordinary ending: an unplug or link loss no longer ends it, so
 * replugging resumes the same session rather than needing a new one.
 * EFI_SUCCESS means the vendor stack answered nothing for a sustained run and
 * the session gave up on its own. Anything else is the platform error from a
 * session that could not be started at all.
 */
EFI_STATUS
SfbMassStorageExportDisk (IN CONST CHAR16 *Name,
                          IN CONST CHAR8  *Tag);
/*
 * The bundled mass-storage driver: loaded and started on first use,
 * presenting the export as 1209:ca0e / fixed disk / canoe strings. Returns
 * NULL when this build carries no bundled driver or it could not start; the
 * caller then falls back to the platform driver. Tried at most once per
 * boot.
 */
SFB_USB_MSD_PROTOCOL *
SfbMsdVariantProtocol (VOID);

#endif
