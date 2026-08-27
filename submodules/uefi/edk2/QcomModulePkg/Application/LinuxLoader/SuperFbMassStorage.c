/*
 * USB mass-storage export over the platform's EFI_USB_MSD_PROTOCOL.
 *
 * The vendor UsbMsdDxe owns everything above BlockIo: the descriptor set,
 * enumeration, EP0 class requests (Get Max LUN, BOT reset), the BOT state
 * machine and the SCSI decoder. The client assigns a BlockIo to a LUN slot,
 * starts the device and pumps events. The protocol shape is mirrored from the
 * public Mu-Silicium EFIUsbMsd.h; the GUID is the one the census finds
 * installed on device (msda=1), and it is the same stack fastboot uses.
 *
 * The export screen is rendered by SfbMassStorageExportDisk for every caller,
 * including fastboot oem mass-storage. Keeping it on the menu path left a
 * stale, unserviced menu painted while the cancel poll silently consumed every
 * keypress, leaving the operator with no visible state or advertised way out.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbMassStorage.h"
#include "SuperFbMenu.h"

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/SimpleTextIn.h>

/*
 * Consecutive EventHandler errors that end a session. Only reached when the
 * vendor stack is answering nothing at all; it exists so a device whose
 * console is unavailable still has a way out, since cancelling needs a key.
 * Any single non-error poll resets the run, so a blip cannot trip it.
 */
#define SFB_MSC_MAX_CONSECUTIVE_ERRORS  100000u

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
SfbMassStorageDrainKeys (VOID)
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
SfbMassStorageCancelled (VOID)
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

typedef struct {
  CONST CHAR16            *Name;
  CONST CHAR8             *Tag;
  EFI_BLOCK_IO_PROTOCOL   *BlockIo;
} SFB_MASS_STORAGE_TARGET;

STATIC
UINT64
SfbMassStoragePartitionBytes (IN EFI_BLOCK_IO_PROTOCOL *BlockIo)
{
  UINT64 Blocks;

  if (BlockIo == NULL || BlockIo->Media == NULL ||
      BlockIo->Media->BlockSize == 0) {
    return 0;
  }
  if (BlockIo->Media->LastBlock == MAX_UINT64 ||
      BlockIo->Media->LastBlock + 1 >
        MAX_UINT64 / BlockIo->Media->BlockSize) {
    return MAX_UINT64;
  }
  Blocks = BlockIo->Media->LastBlock + 1;
  return Blocks * BlockIo->Media->BlockSize;
}

EFI_STATUS
SfbMassStorageExportDisk (IN EFI_BLOCK_IO_PROTOCOL *BlockIo,
                          IN CONST CHAR8           *Tag)
{
  EFI_STATUS            Status;
  SFB_USB_MSD_PROTOCOL *Msd = NULL;
  UINT8                 MaxLun = 0;
  BOOLEAN               Cancelled = FALSE;
  BOOLEAN               HandlerErrorLogged = FALSE;
  UINT32                Polls = 0;
  UINT32                NotReady = 0;
  UINT32                Errors = 0;
  UINT32                Consecutive = 0;

  if (BlockIo == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Status = gBS->LocateProtocol ((EFI_GUID *)&mSfbUsbMsdProtocolGuid, NULL,
                                (VOID **)&Msd);
  if (EFI_ERROR (Status) || Msd == NULL) {
    SfbUsbCensus ();
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK msc-run target=%a status=%r reason=protocol\n",
            (Tag != NULL) ? Tag : "?", Status));
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
    SfbUsbCensus ();
    DEBUG ((EFI_D_ERROR, "SFB: MARK msc-run target=%a status=%r reason=assign\n",
            (Tag != NULL) ? Tag : "?", Status));
    return Status;
  }
  /*
   * Draw after assigning the LUN and before draining keys. The chooser's
   * confirm keystroke is still queued here, so the screen must be visible
   * before the drain hands control to the export loop.
   */
  SfbBeginScreen (L"USB Mass Storage", L"The host may now mount the disk.");
  Print (L"Partition: %a\r\n", (Tag != NULL) ? Tag : "?");
  Print (L"Size: %Lu bytes\r\n", SfbMassStoragePartitionBytes (BlockIo));
  Print (L"\r\nVolume Down ends this session.\r\n");
  SfbEndScreen (L"Volume Down: stop export");

  /* Nothing queued may reach the cancel test: the confirm press that opened
   * this screen is still in the queue at this point. */
  SfbMassStorageDrainKeys ();

  /*
   * An export can legitimately sit idle for as long as the operator leaves it
   * up, and the UEFI default watchdog is five minutes. Both the Mu-Silicium
   * reference client and this tree's own fastboot path disable it before
   * taking the link (FastbootCmds.c), and neither restores it: a bootloader
   * menu that waits at a prompt has no business being reset by a watchdog.
   */
  gBS->SetWatchdogTimer (0, 0x10000, 0, NULL);

  Status = Msd->StartDevice (Msd);
  if (EFI_ERROR (Status)) {
    SfbUsbCensus ();
    DEBUG ((EFI_D_ERROR, "SFB: MARK msc-run target=%a status=%r reason=start\n",
            (Tag != NULL) ? Tag : "?", Status));
    /*
     * StartDevice may have partially claimed the shared gadget before
     * reporting failure. Stop it before releasing the LUN so a failed start
     * cannot leave fastboot or the partition in a half-owned state.
     */
    Msd->StopDevice (Msd);
    Msd->AssignBlkIoHandle (Msd, NULL, 0);
    return Status;
  }
  DEBUG ((EFI_D_INFO, "SFB: MARK msc-started target=%a\n",
          (Tag != NULL) ? Tag : "?"));

  /*
   * Pump the handler first and test for cancel second, which is the order the
   * Mu-Silicium reference client uses
   * (QcomPkg/Applications/MassStorage/MassStorage.c). The first poll then lands
   * before any console access, which is where it is needed: the host begins
   * enumerating the moment StartDevice returns and Linux scans one second
   * later.
   *
   * The return value is counted but a single error no longer ends the session.
   * The vendor header documents EventHandler's own error returns as literally
   * "?" (QcomPkg/Include/Protocol/EFIUsbMsd.h) and the reference client
   * discards the value entirely. Treating one transient error as terminal used
   * to break this loop and tear the gadget down while the host was still
   * scanning, which presents exactly as a device that enumerates but never
   * offers a disk.
   *
   * Only an unbroken run of errors ends it. That keeps a way out when the
   * console is unavailable and SfbMassStorageCancelled can never return TRUE,
   * while being far beyond anything a blip during enumeration produces; any
   * single good poll resets the count.
   */
  while (TRUE) {
    Status = Msd->EventHandler (Msd);
    Polls++;
    if (Status == EFI_NOT_READY) {
      NotReady++;
      Consecutive = 0;
    } else if (EFI_ERROR (Status)) {
      Errors++;
      Consecutive++;
      if (!HandlerErrorLogged) {
        DEBUG ((EFI_D_ERROR,
                "SFB: MARK msc-handler target=%a status=%r\n",
                (Tag != NULL) ? Tag : "?", Status));
        HandlerErrorLogged = TRUE;
      }
      if (Consecutive >= SFB_MSC_MAX_CONSECUTIVE_ERRORS) {
        break;
      }
    } else {
      Consecutive = 0;
    }

    if (SfbMassStorageCancelled ()) {
      Cancelled = TRUE;
      break;
    }
  }

  Msd->StopDevice (Msd);
  Msd->AssignBlkIoHandle (Msd, NULL, 0);

  /* Poll counts make a starved loop visible in the log: a session that lasted
   * seconds but polled only a few hundred times is not servicing the link. */
  DEBUG ((EFI_D_ERROR,
          "SFB: MARK msc-poll target=%a polls=%u notready=%u errors=%u\n",
          (Tag != NULL) ? Tag : "?", Polls, NotReady, Errors));
  DEBUG ((EFI_D_ERROR,
          "SFB: MARK msc-run target=%a status=%r reason=%a\n",
          (Tag != NULL) ? Tag : "?", Status,
          Cancelled ? "cancelled" : "handler-error"));
  return Cancelled ? EFI_ABORTED : EFI_SUCCESS;
}

STATIC
VOID
SfbMassStorageDrawTarget (IN CONST SFB_MASS_STORAGE_TARGET *Target,
                          IN BOOLEAN                         Selected)
{
  CHAR16 Text[128];
  UINT64 Size;

  Size = SfbMassStoragePartitionBytes (Target->BlockIo);
  UnicodeSPrint (Text, sizeof (Text), L"%s (%Lu MiB)",
                 Target->Name, Size / (1024 * 1024));
  SfbDrawRow (Selected, L" ", Text);
}

/*
 * persist is the live ext4 volume owned by the running system. A host write
 * while Android is still using that filesystem can corrupt the boot root, so
 * the warning is deliberately a separate confirmation screen rather than a
 * footnote in the chooser.
 */
STATIC
BOOLEAN
SfbMassStorageConfirmPersist (VOID)
{
  STATIC CONST CHAR16 *Rows[] = {
    L"Export persist",
    L"Back"
  };
  UINTN Cursor = 0;

  while (TRUE) {
    SFB_KEY Key;

    SfbBeginScreen (L"WARNING: live persist",
                    L"The running system also owns this filesystem.");
    Print (L"A host writing persist while the device is elsewhere can\r\n");
    Print (L"corrupt canoe.cfg, boot.efi, or its sidecars.\r\n");
    Print (L"\r\n");
    SfbDrawRow ((BOOLEAN)(Cursor == 0), L" ", Rows[0]);
    SfbDrawRow ((BOOLEAN)(Cursor == 1), L" ", Rows[1]);
    SfbEndScreen (L"Vol Up/Down: move   Power: select");

    Key = SfbWaitForKey (0);
    if (Key == SfbKeyUp || Key == SfbKeyDown) {
      SfbMoveCursor (&Cursor, ARRAY_SIZE (Rows), Key);
      continue;
    }
    return (BOOLEAN)(Cursor == 0 && Key == SfbKeySelect);
  }
}

VOID
SfbRunMassStorageMenu (VOID)
{
  SFB_MASS_STORAGE_TARGET Targets[2];
  UINTN                   Count = 0;
  UINTN                   Cursor = 0;
  UINTN                   Index;

  Targets[0].Name = L"persist";
  Targets[0].Tag = "persist";
  Targets[0].BlockIo = NULL;
  Targets[1].Name = L"logfs";
  Targets[1].Tag = "logfs";
  Targets[1].BlockIo = NULL;

  /*
   * Resolve each target at draw time. logfs is optional, and hiding an absent
   * partition is less misleading than offering a row that can never start.
   */
  for (Index = 0; Index < ARRAY_SIZE (Targets); Index++) {
    EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
    EFI_STATUS             Status;

    Status = SfbFindPartitionByName (Targets[Index].Name, &BlockIo);
    if (EFI_ERROR (Status) || BlockIo == NULL) {
      continue;
    }
    Targets[Count] = Targets[Index];
    Targets[Count].BlockIo = BlockIo;
    Count++;
  }

  while (TRUE) {
    SFB_KEY Key;

    if (Count == 0) {
      Cursor = 0;
    } else if (Cursor >= Count + 1) {
      Cursor = Count;
    }

    SfbBeginScreen (L"USB Mass Storage",
                    L"Choose one partition to export to the host.");
    for (Index = 0; Index < Count; Index++) {
      SfbMassStorageDrawTarget (&Targets[Index],
                                (BOOLEAN)(Cursor == Index));
    }
    SfbDrawRow ((BOOLEAN)(Cursor == Count), L" ", L"Back");
    SfbEndScreen (L"Vol Up/Down: move   Power: select");

    Key = SfbWaitForKey (0);
    if (Key == SfbKeyUp || Key == SfbKeyDown) {
      SfbMoveCursor (&Cursor, Count + 1, Key);
      continue;
    }
    if (Key != SfbKeySelect || Cursor == Count) {
      return;
    }

    if (StrCmp (Targets[Cursor].Name, L"persist") == 0 &&
        !SfbMassStorageConfirmPersist ()) {
      continue;
    }

    {
      EFI_STATUS Status;

      Status = SfbMassStorageExportDisk (Targets[Cursor].BlockIo,
                                         Targets[Cursor].Tag);
      if (EFI_ERROR (Status) && Status != EFI_ABORTED) {
        SfbReportStatus (L"Could not start mass storage", Status);
      }
    }
  }
}

/*
 * SfbMassStorageExportDisk draws the export screen for every caller,
 * including this fastboot oem mass-storage path and the interactive menu.
 * Previously that path left a stale, unserviced menu painted while the cancel
 * poll silently consumed every keypress, leaving no visible state or
 * advertised way out.
 */
EFI_STATUS
SfbExportPartitionByName (IN CONST CHAR16 *Target)
{
  EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
  CONST CHAR8           *Tag;
  EFI_STATUS             Status;

  if (Target == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (StrCmp (Target, L"persist") == 0) {
    Tag = "persist";
  } else if (StrCmp (Target, L"logfs") == 0) {
    Tag = "logfs";
  } else {
    return EFI_INVALID_PARAMETER;
  }

  Status = SfbFindPartitionByName (Target, &BlockIo);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (BlockIo == NULL) {
    return EFI_DEVICE_ERROR;
  }
  return SfbMassStorageExportDisk (BlockIo, Tag);
}
