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
 * The bundled canoe variant of the platform UsbMsdDxe: loaded and started
 * on first use, presenting the export as 1209:ca0e / fixed disk / canoe
 * strings. Returns NULL when this build carries no variant or it could not
 * start; callers fall back to the resident driver. Tried at most once per
 * boot.
 */
SFB_USB_MSD_PROTOCOL *
SfbMsdVariantProtocol (VOID);

/*
 * Export a copy of the resident driver's loaded image as a read-only
 * RAM-disk LUN, so any target can hand its exact blob to the host for
 * calibration without any filesystem write support on the device.
 */
EFI_STATUS
SfbExportMsdImage (VOID);

#endif
