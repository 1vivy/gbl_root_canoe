/*
 * USB mass-storage export over the platform's EFI_USB_MSD_PROTOCOL.
 *
 * The driver behind that protocol owns everything above BlockIo: the
 * descriptor set, enumeration, EP0 class requests (Get Max LUN, BOT reset),
 * the BOT state machine and the SCSI decoder. The client assigns a BlockIo to
 * a LUN slot, starts the device and pumps events. It is the same stack
 * fastboot uses.
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

#include <FastbootLib/FastbootMain.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
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

/* The protocol mirror lives in SuperFbMassStorage.h, shared with the bundled
 * driver loader. */

/*
 * UsbfnIo is declared in QcomModulePkg.dec. The MSD protocol GUID is the one
 * the census finds installed on device.
 */
STATIC CONST EFI_GUID mSfbUsbfnIoProtocolGuid = {
  0x32d2963a, 0xfe5d, 0x4f30,
  { 0xb6, 0x33, 0x6e, 0x5d, 0xc5, 0x58, 0x03, 0xcc }
};
STATIC CONST EFI_GUID mSfbUsbMsdProtocolGuid = {
  0xc8591faf, 0xdbcc, 0x479e,
  { 0x9e, 0xf2, 0xfd, 0x08, 0x5b, 0xc3, 0x7b, 0xc7 }
};

/*
 * The driver's StartDevice locates EFI_USBFN_IO_PROTOCOL, which does not
 * exist until the platform USB controller has been initialised. Fastboot does
 * that on entry, so an `oem mass-storage` export inherits a live stack; a
 * menu export on a normal boot does not, and StartDevice fails with
 * EFI_NOT_FOUND.
 *
 * Measured on the OnePlus 15: a fastboot-entered session censuses usbfn=1
 * over 367 handles, a flashed efisp boot usbfn=0 over 363. Every USB export
 * this tree ever proved was reached through fastboot, which is why this went
 * unnoticed until efisp carried the loader.
 *
 * The bring-up claims no gadget and installs no descriptors, so this is a
 * no-op wherever USB is already up, and it needs no matching release when the
 * export ends.
 */
STATIC
VOID
SfbMassStorageEnsureUsbStack (VOID)
{
  EFI_STATUS Status;
  VOID       *Protocol = NULL;
  BOOLEAN    Present;

  Status = gBS->LocateProtocol ((EFI_GUID *)&mSfbUsbfnIoProtocolGuid, NULL,
                                &Protocol);
  if (!EFI_ERROR (Status)) {
    return;
  }

  Status = SfbUsbControllerInit ();
  Protocol = NULL;
  Present = (BOOLEAN)!EFI_ERROR (
      gBS->LocateProtocol ((EFI_GUID *)&mSfbUsbfnIoProtocolGuid, NULL,
                           &Protocol));
  DEBUG ((EFI_D_ERROR, "SFB: MARK msc-usb-init status=%r usbfn=%u\n",
          Status, (UINT32)Present));
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
SfbMassStorageExportDisk (IN CONST CHAR16 *Name,
                          IN CONST CHAR8  *Tag)
{
  EFI_STATUS            Status;
  EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
  SFB_USB_MSD_PROTOCOL *Msd = NULL;
  UINT8                 MaxLun = 0;
  BOOLEAN               Cancelled = FALSE;
  BOOLEAN               HandlerErrorLogged = FALSE;
  UINT32                Polls = 0;
  UINT32                NotReady = 0;
  UINT32                Errors = 0;
  UINT32                Consecutive = 0;

  if (Name == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  /*
   * The USB stack is this session's one external prerequisite; settle it
   * before any driver state is touched.
   */
  SfbMassStorageEnsureUsbStack ();

  /*
   * Only now is a partition handle worth resolving. The release above ran
   * DisconnectController over the host controllers, which tears down every
   * child handle hanging off them, and the ensure reconnected the tree; a
   * Block I/O looked up before either step would be a freed interface.
   */
  Status = SfbFindPartitionByName (Name, &BlockIo);
  if (EFI_ERROR (Status) || BlockIo == NULL) {
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK msc-run target=%a status=%r reason=partition\n",
            (Tag != NULL) ? Tag : "?", Status));
    return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
  }
  /*
   * The bundled driver is preferred: its identity (1209:ca0e, fixed disk)
   * matches no host-side rule that would tear the session down. The platform
   * driver (05c6:f000) is the fallback when this build carries no bundled
   * driver or it could not start.
   */
  Msd = SfbMsdVariantProtocol ();
  if (Msd == NULL) {
    Status = gBS->LocateProtocol ((EFI_GUID *)&mSfbUsbMsdProtocolGuid, NULL,
                                  (VOID **)&Msd);
    if (EFI_ERROR (Status) || Msd == NULL) {
      DEBUG ((EFI_D_ERROR,
              "SFB: MARK msc-run target=%a status=%r reason=protocol\n",
              (Tag != NULL) ? Tag : "?", Status));
      return EFI_NOT_FOUND;
    }
  }

  Status = Msd->QueryMaxLun (Msd, &MaxLun);
  if (EFI_ERROR (Status)) {
    MaxLun = 0;
  }
  DEBUG ((EFI_D_INFO,
          "SFB: MARK msc-export target=%a driver=%a maxlun=%u revision=0x%x\n",
          (Tag != NULL) ? Tag : "?",
          (Msd == SfbMsdVariantProtocol ()) ? "bundled" : "platform",
          (UINT32)MaxLun, Msd->Revision));

  Status = Msd->AssignBlkIoHandle (Msd, BlockIo, 0);
  if (EFI_ERROR (Status)) {
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
   * up. The architectural watchdog is already off on both routes here: the
   * loader disables it before the first prompt, and the vendor's fastboot path
   * disables it again on entry (FastbootCmds.c), which covers `oem
   * mass-storage`. Nothing between those points arms it, so there is no third
   * disable in this function.
   */
  Status = Msd->StartDevice (Msd);
  if (EFI_ERROR (Status)) {
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

  /*
   * Drain on the way out as well as on the way in. Volume Down itself is
   * consumed by SfbMassStorageCancelled, but its trailing events and anything
   * the operator pressed while the host was mounting are still queued, and
   * the next screen receives them as its own input. On the menu path that
   * next screen is the chooser and then the rebuilt boot menu, whose first
   * row is "Enter Fastboot": a stray confirm makes the session look like it
   * ended straight into fastboot mode, which is not a place the operator
   * asked to be and has no way back to the menu.
   */
  SfbMassStorageDrainKeys ();

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
VOID
SfbDrawPersistWarning (IN VOID *Context)
{
  (VOID)Context;
  Print (L"A host writing persist while the device is elsewhere can\r\n");
  Print (L"corrupt canoe.cfg, boot.efi, or its sidecars.\r\n");
  Print (L"\r\n");
}

STATIC
SFB_MENU_ACTION
SfbHandlePersistConfirm (IN VOID *Context,
                         IN UINTN Row,
                         IN SFB_KEY Key)
{
  BOOLEAN *Confirmed = (BOOLEAN *)Context;

  *Confirmed = (BOOLEAN)(Key == SfbKeySelect && Row == 0);
  return SfbMenuActionExit;
}

STATIC
BOOLEAN
SfbMassStorageConfirmPersist (VOID)
{
  STATIC SFB_MENU_ROW Rows[] = {
    { L"Export persist", L" " },
    { L"Back", L" " }
  };
  SFB_MENU_TEMPLATE Template;
  BOOLEAN Confirmed = FALSE;

  ZeroMem (&Template, sizeof (Template));
  Template.Title = L"WARNING: live persist";
  Template.Subtitle = L"The running system also owns this filesystem.";
  Template.Footer = L"Vol Up/Down: move   Power: select";
  Template.Rows = Rows;
  Template.RowCount = ARRAY_SIZE (Rows);
  Template.Navigate = TRUE;
  Template.Context = &Confirmed;
  Template.Enter = SfbMenuNoopEnter;
  Template.Exit = SfbMenuNoopExit;
  Template.Handler = SfbHandlePersistConfirm;
  Template.DrawHeader = SfbDrawPersistWarning;
  (VOID)SfbRunMenu (&Template);
  return Confirmed;
}

typedef struct {
  SFB_MASS_STORAGE_TARGET Targets[2];
  UINTN                   Count;
  SFB_MENU_TEMPLATE      *Template;
} SFB_MASS_STORAGE_MENU_CONTEXT;

STATIC
EFI_STATUS
SfbRefreshMassStorageMenu (IN VOID *Context)
{
  STATIC CONST SFB_MASS_STORAGE_TARGET Probe[] = {
    { L"persist", "persist", NULL },
    { L"logfs",   "logfs",   NULL }
  };
  SFB_MASS_STORAGE_MENU_CONTEXT *State =
    (SFB_MASS_STORAGE_MENU_CONTEXT *)Context;
  UINTN Index;

  /*
   * Resolve on every redraw, not once on entry. An export tears down and
   * rebuilds the partition tree, so a pointer cached before it is stale.
   */
  State->Count = 0;
  for (Index = 0; Index < ARRAY_SIZE (Probe); Index++) {
    EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;

    if (EFI_ERROR (SfbFindPartitionByName (Probe[Index].Name, &BlockIo)) ||
        BlockIo == NULL) {
      continue;
    }
    State->Targets[State->Count] = Probe[Index];
    State->Targets[State->Count].BlockIo = BlockIo;
    State->Count++;
  }
  State->Template->RowCount = State->Count + 1;
  if (State->Template->Cursor > State->Count) {
    State->Template->Cursor = State->Count;
  }
  return EFI_SUCCESS;
}

STATIC
VOID
SfbDrawMassStorageMenuRow (IN VOID *Context,
                           IN UINTN Row,
                           IN BOOLEAN Selected)
{
  SFB_MASS_STORAGE_MENU_CONTEXT *State =
    (SFB_MASS_STORAGE_MENU_CONTEXT *)Context;

  if (Row == State->Count) {
    SfbDrawRow (Selected, L" ", L"Back");
  } else {
    SfbMassStorageDrawTarget (&State->Targets[Row], Selected);
  }
}

STATIC
SFB_MENU_ACTION
SfbHandleMassStorageMenuRow (IN VOID *Context,
                             IN UINTN Row,
                             IN SFB_KEY Key)
{
  SFB_MASS_STORAGE_MENU_CONTEXT *State =
    (SFB_MASS_STORAGE_MENU_CONTEXT *)Context;
  EFI_STATUS Status;

  if (Row >= State->Count || Key != SfbKeySelect) {
    return SfbMenuActionExit;
  }
  if (StrCmp (State->Targets[Row].Name, L"persist") == 0 &&
      !SfbMassStorageConfirmPersist ()) {
    return SfbMenuActionContinue;
  }

  Status = SfbMassStorageExportDisk (State->Targets[Row].Name,
                                     State->Targets[Row].Tag);
  if (EFI_ERROR (Status) && Status != EFI_ABORTED) {
    SfbReportStatus (L"Could not start mass storage", Status);
  }
  return SfbMenuActionRebuild;
}

VOID
SfbRunMassStorageMenu (VOID)
{
  SFB_MASS_STORAGE_MENU_CONTEXT Context;
  SFB_MENU_TEMPLATE             Template;

  ZeroMem (&Context, sizeof (Context));
  ZeroMem (&Template, sizeof (Template));
  Context.Template = &Template;
  Template.Title = L"USB Mass Storage";
  Template.Subtitle = L"Choose one partition to export to the host.";
  Template.Footer = L"Vol Up/Down: move   Power: select";
  Template.Navigate = TRUE;
  Template.Context = &Context;
  Template.Enter = SfbMenuNoopEnter;
  Template.Exit = SfbMenuNoopExit;
  Template.Refresh = SfbRefreshMassStorageMenu;
  Template.Handler = SfbHandleMassStorageMenuRow;
  Template.DrawRow = SfbDrawMassStorageMenuRow;
  (VOID)SfbRunMenu (&Template);
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
  CONST CHAR8 *Tag;

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

  /* The partition is resolved inside the export, after it has released host
   * mode; looking it up here would hand over a handle that release frees. */
  return SfbMassStorageExportDisk (Target, Tag);
}
