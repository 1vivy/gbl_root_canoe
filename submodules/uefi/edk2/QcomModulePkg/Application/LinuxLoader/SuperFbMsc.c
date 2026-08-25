/*
 * USB mass-storage export over the platform's EFI_USB_MSD_PROTOCOL.
 *
 * The vendor UsbMsdDxe owns everything above BlockIo: the descriptor set,
 * enumeration, EP0 class requests (Get Max LUN, BOT reset), the BOT state
 * machine and the SCSI decoder. The client assigns a BlockIo to a LUN slot,
 * starts the device and pumps events. The protocol shape is mirrored from
 * the public Mu-Silicium EFIUsbMsd.h; the GUID is the one the census finds
 * installed on device (msda=1), and it is the same stack fastboot uses.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbMsc.h"

#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/SimpleTextIn.h>

/* EFI_USB_MSD_PROTOCOL, mirrored from the public Mu-Silicium EFIUsbMsd.h. */
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
 * UsbfnIo and UsbDevice are declared in QcomModulePkg.dec. The MSD protocol
 * GUID is EFI_USB_MSD_PROTOCOL_GUID from the public EFIUsbMsd.h, confirmed
 * installed on device by the census; the module GUID and candidate B remain
 * unconfirmed extractions from the same-platform UsbMsdDxe.efi, kept so the
 * census can report them.
 */
STATIC CONST EFI_GUID mSfbUsbfnIoProtocolGuid = {
  0x32d2963a, 0xfe5d, 0x4f30,
  { 0xb6, 0x33, 0x6e, 0x5d, 0xc5, 0x58, 0x03, 0xcc }
};
STATIC CONST EFI_GUID mSfbUsbDeviceProtocolGuid = {
  0xd9d9ce48, 0x44b8, 0x4f49,
  { 0x8e, 0x3e, 0x2a, 0x3b, 0x92, 0x7d, 0xc6, 0xc1 }
};
STATIC CONST EFI_GUID mSfbUsbMsdModuleGuid = {
  0x5af77f10, 0x90df, 0x4e7e,
  { 0x83, 0x25, 0xa1, 0x7e, 0xc0, 0x9d, 0x54, 0x43 }
};
STATIC CONST EFI_GUID mSfbUsbMsdProtocolGuid = {
  0xc8591faf, 0xdbcc, 0x479e,
  { 0x9e, 0xf2, 0xfd, 0x08, 0x5b, 0xc3, 0x7b, 0xc7 }
};
STATIC CONST EFI_GUID mSfbUsbMsdCandidateBGuid = {
  0x88e71196, 0x80d3, 0x4548,
  { 0x93, 0x6e, 0x57, 0x09, 0x3f, 0x6d, 0xd2, 0x11 }
};

VOID
SfbUsbCensus (VOID)
{
  EFI_STATUS Status;
  EFI_HANDLE *Handles = NULL;
  UINTN      HandleCount = 0;
  VOID       *Protocol = NULL;
  UINTN      Usbfn = 0;
  UINTN      UsbDevice = 0;
  UINTN      MsdModule = 0;
  UINTN      MsdProtocol = 0;
  UINTN      MsdCandidateB = 0;

  Status = gBS->LocateHandleBuffer (AllHandles, NULL, NULL, &HandleCount,
                                    &Handles);
  if (EFI_ERROR (Status)) {
    HandleCount = 0;
  }
  Status = gBS->LocateProtocol ((EFI_GUID *)&mSfbUsbfnIoProtocolGuid, NULL,
                                &Protocol);
  Usbfn = (UINTN)!EFI_ERROR (Status);
  Status = gBS->LocateProtocol ((EFI_GUID *)&mSfbUsbDeviceProtocolGuid, NULL,
                                &Protocol);
  UsbDevice = (UINTN)!EFI_ERROR (Status);
  Status = gBS->LocateProtocol ((EFI_GUID *)&mSfbUsbMsdModuleGuid, NULL,
                                &Protocol);
  MsdModule = (UINTN)!EFI_ERROR (Status);
  Status = gBS->LocateProtocol ((EFI_GUID *)&mSfbUsbMsdProtocolGuid, NULL,
                                &Protocol);
  MsdProtocol = (UINTN)!EFI_ERROR (Status);
  Status = gBS->LocateProtocol ((EFI_GUID *)&mSfbUsbMsdCandidateBGuid, NULL,
                                &Protocol);
  MsdCandidateB = (UINTN)!EFI_ERROR (Status);

  DEBUG ((EFI_D_INFO,
          "SFB: MARK usb-census handles=%u usbfn=%u usbdev=%u msdmod=%u msda=%u msdb=%u\n",
          (UINT32)HandleCount, (UINT32)Usbfn, (UINT32)UsbDevice,
          (UINT32)MsdModule, (UINT32)MsdProtocol, (UINT32)MsdCandidateB));
  if (Handles != NULL) {
    FreePool (Handles);
  }
}

/*
 * Discard anything already queued. The chooser screen that precedes the
 * export hands over with the operator's confirm keystroke, and sometimes its
 * trailing event, still in the queue; a cancel test that accepted those would
 * abort the session before the host ever saw the device.
 */
STATIC
VOID
SfbMscDrainKeys (VOID)
{
  EFI_INPUT_KEY Key;
  UINTN         Drained = 0;

  if (gST == NULL || gST->ConIn == NULL) {
    return;
  }
  while (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
    Drained++;
  }
  if (Drained != 0) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK msc-drained keys=%u last-scan=0x%x\n",
            (UINT32)Drained, Key.ScanCode));
  }
}

/*
 * Cancel on volume-down only. "Any key" made the confirm press that started
 * the session cancel it, and it also meant a stray keypress could yank a disk
 * out from under a host mid-write.
 */
STATIC
BOOLEAN
SfbMscCancelled (VOID)
{
  EFI_INPUT_KEY Key;

  if (gST == NULL || gST->ConIn == NULL) {
    return FALSE;
  }
  if (EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
    return FALSE;
  }
  /* Log every key the run loop sees so unexpected sources are named in the
   * boot log, not guessed. */
  DEBUG ((EFI_D_ERROR, "SFB: MARK msc-key scancode=0x%x char=0x%x\n",
          Key.ScanCode, Key.UnicodeChar));
  return (BOOLEAN)(Key.ScanCode == SCAN_DOWN);
}

EFI_STATUS
SfbMscExportDisk (IN EFI_BLOCK_IO_PROTOCOL *BlockIo, IN CONST CHAR8 *Tag)
{
  EFI_STATUS            Status;
  SFB_USB_MSD_PROTOCOL *Msd = NULL;
  UINT8                 MaxLun = 0;
  BOOLEAN               Cancelled = FALSE;

  if (BlockIo == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Status = gBS->LocateProtocol ((EFI_GUID *)&mSfbUsbMsdProtocolGuid, NULL,
                                (VOID **)&Msd);
  if (EFI_ERROR (Status) || Msd == NULL) {
    return EFI_NOT_FOUND;
  }

  Status = Msd->QueryMaxLun (Msd, &MaxLun);
  if (EFI_ERROR (Status)) {
    MaxLun = 0;
  }
  DEBUG ((EFI_D_INFO, "SFB: MARK msc-export target=%a maxlun=%u revision=0x%x\n",
          (Tag != NULL) ? Tag : "?", (UINT32)MaxLun, Msd->Revision));

  Status = Msd->AssignBlkIoHandle (Msd, BlockIo, 0);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK msc-run target=%a status=%r reason=assign\n",
            (Tag != NULL) ? Tag : "?", Status));
    return Status;
  }

  /* Nothing queued may reach the cancel test: the confirm press that opened
   * this screen is still in the queue at this point. */
  SfbMscDrainKeys ();

  Status = Msd->StartDevice (Msd);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK msc-run target=%a status=%r reason=start\n",
            (Tag != NULL) ? Tag : "?", Status));
    Msd->AssignBlkIoHandle (Msd, NULL, 0);
    return Status;
  }
  DEBUG ((EFI_D_INFO, "SFB: MARK msc-started target=%a\n",
          (Tag != NULL) ? Tag : "?"));

  while (TRUE) {
    if (SfbMscCancelled ()) {
      Cancelled = TRUE;
      Status = EFI_ABORTED;
      break;
    }
    Status = Msd->EventHandler (Msd);
    if (EFI_ERROR (Status)) {
      /* The platform reports host unplug and link loss this way; the session
       * is over either way. */
      break;
    }
    gBS->Stall (1000);
  }

  Msd->StopDevice (Msd);
  Msd->AssignBlkIoHandle (Msd, NULL, 0);

  DEBUG ((EFI_D_ERROR, "SFB: MARK msc-run target=%a status=%r reason=%a\n",
          (Tag != NULL) ? Tag : "?", Status,
          Cancelled ? "cancelled" : "host-gone"));
  return Cancelled ? EFI_ABORTED : EFI_SUCCESS;
}
