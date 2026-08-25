/*
 * USB mass-storage Bulk-Only Transport target.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbMsc.h"

STATIC
UINT32
SfbMscLe32 (IN CONST UINT8 *Bytes)
{
  return (UINT32)Bytes[0] | ((UINT32)Bytes[1] << 8) |
         ((UINT32)Bytes[2] << 16) | ((UINT32)Bytes[3] << 24);
}

EFI_STATUS
SfbMscParseCbw (
  IN CONST VOID *Buffer,
  IN UINTN       BufferBytes,
  OUT SFB_MSC_CBW *Cbw,
  IN UINTN       LunCount
  )
{
  CONST UINT8 *Bytes;
  UINTN        Index;

  if (Buffer == NULL || Cbw == NULL || BufferBytes != SFB_MSC_CBW_BYTES ||
      LunCount == 0 || LunCount > SFB_MSC_MAX_LUNS) {
    return EFI_INVALID_PARAMETER;
  }

  Bytes = (CONST UINT8 *)Buffer;
  if (SfbMscLe32 (Bytes) != SFB_MSC_CBW_SIGNATURE ||
      (Bytes[12] & 0x7F) != 0 || Bytes[14] == 0 || Bytes[14] > 16 ||
      Bytes[13] >= LunCount) {
    return EFI_COMPROMISED_DATA;
  }

  Cbw->Signature = SFB_MSC_CBW_SIGNATURE;
  Cbw->Tag = SfbMscLe32 (Bytes + 4);
  Cbw->DataTransferLength = SfbMscLe32 (Bytes + 8);
  Cbw->Flags = Bytes[12];
  Cbw->Lun = Bytes[13];
  Cbw->CdbLength = Bytes[14];
  for (Index = 0; Index < sizeof (Cbw->Cdb); Index++) {
    Cbw->Cdb[Index] = Bytes[15 + Index];
  }
  return EFI_SUCCESS;
}

VOID
SfbMscBuildCsw (
  OUT SFB_MSC_CSW *Csw,
  IN UINT32        Tag,
  IN UINT32        Residue,
  IN UINT8         Status
  )
{
  if (Csw == NULL) {
    return;
  }
  Csw->Signature = SFB_MSC_CSW_SIGNATURE;
  Csw->Tag = Tag;
  Csw->DataResidue = Residue;
  Csw->Status = Status;
}

#ifndef SFB_HOST_BUILD

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/EFIUsbDevice.h>
#include <Protocol/SimpleTextIn.h>
/*
 * UsbfnIo and UsbDevice are declared in QcomModulePkg.dec. The remaining
 * GUIDs are unconfirmed candidates extracted from the same-platform
 * UsbMsdDxe.efi.
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
STATIC CONST EFI_GUID mSfbUsbMsdCandidateAGuid = {
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
  UINTN      MsdCandidateA = 0;
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
  Status = gBS->LocateProtocol ((EFI_GUID *)&mSfbUsbMsdCandidateAGuid, NULL,
                                &Protocol);
  MsdCandidateA = (UINTN)!EFI_ERROR (Status);
  Status = gBS->LocateProtocol ((EFI_GUID *)&mSfbUsbMsdCandidateBGuid, NULL,
                                &Protocol);
  MsdCandidateB = (UINTN)!EFI_ERROR (Status);

  DEBUG ((EFI_D_INFO,
          "SFB: MARK usb-census handles=%u usbfn=%u usbdev=%u msdmod=%u msda=%u msdb=%u\n",
          (UINT32)HandleCount, (UINT32)Usbfn, (UINT32)UsbDevice,
          (UINT32)MsdModule, (UINT32)MsdCandidateA, (UINT32)MsdCandidateB));
  if (Handles != NULL) {
    FreePool (Handles);
  }
}


extern EFI_STATUS
SfbMscBuildDescriptorSet (OUT USB_DEVICE_DESCRIPTOR_SET *DescriptorSet);

#define USB_INDEX_TO_EP(Index)  ((Index) & 0x0f)

#define SFB_MSC_EP_OUT  0x01
#define SFB_MSC_EP_IN   0x81
#define SFB_MSC_EP_NUM  1
#define SFB_MSC_BOT_OK  0
#define SFB_MSC_BOT_FAIL 1
#define SFB_MSC_BOT_PHASE 2

typedef enum {
  SfbMscStateCbw,
  SfbMscStateDataIn,
  SfbMscStateDataOut,
  SfbMscStateCsw
} SFB_MSC_STATE;

typedef struct {
  EFI_USB_DEVICE_PROTOCOL  *Usb;
  CONST SFB_MSC_LUN        *Luns;
  UINTN                     LunCount;
  SFB_MSC_SENSE             Sense[SFB_MSC_MAX_LUNS];
  VOID                     *RxBuffer;
  VOID                     *TxBuffer;
  SFB_MSC_CBW               Cbw;
  SFB_MSC_SCSI_RESPONSE     Response;
  SFB_MSC_STATE              State;
  UINT32                    BlocksLeft;
  EFI_LBA                   CurrentLba;
  UINT32                    HostLength;
  UINT32                    Transferred;
  UINT32                    ChunkBytes;
  UINT8                     BotStatus;
  BOOLEAN                   Ejected[SFB_MSC_MAX_LUNS];
  BOOLEAN                   Started;
  BOOLEAN                   Connected;
  BOOLEAN                   Disconnected;
  /* Commands the host actually issued. Zero with Connected set means the bus
   * enumerated us and the host then asked nothing, which is a different
   * failure from never being seen at all. */
  UINT32                    CbwCount;
} SFB_MSC_CONTEXT;

STATIC
BOOLEAN
SfbMscAllEjected (IN CONST SFB_MSC_CONTEXT *Context)
{
  UINTN Index;

  for (Index = 0; Index < Context->LunCount; Index++) {
    if (!Context->Ejected[Index]) {
      return FALSE;
    }
  }
  return TRUE;
}

/*
 * Discard anything already queued. The menu screen that precedes this hands
 * over with the operator's confirm keystroke, and sometimes its trailing event,
 * still in the queue; a cancel test that accepted those would abort the session
 * before the host ever saw the device.
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
  /* Sessions end with reason=cancelled and no operator keypress: log every
   * key the run loop sees so the phantom source is named, not guessed. */
  DEBUG ((EFI_D_ERROR, "SFB: MARK msc-key scancode=0x%x char=0x%x\n",
          Key.ScanCode, Key.UnicodeChar));
  return (BOOLEAN)(Key.ScanCode == SCAN_DOWN);
}

STATIC
EFI_STATUS
SfbMscQueueCbw (IN OUT SFB_MSC_CONTEXT *Context)
{
  Context->State = SfbMscStateCbw;
  return Context->Usb->Send (SFB_MSC_EP_OUT, SFB_MSC_CBW_BYTES,
                             Context->RxBuffer);
}

STATIC
EFI_STATUS
SfbMscQueueCsw (IN OUT SFB_MSC_CONTEXT *Context)
{
  SFB_MSC_CSW Csw;
  UINT32      Residue;
  UINTN       Index;

  if (Context->HostLength >= Context->Transferred) {
    Residue = Context->HostLength - Context->Transferred;
  } else {
    Residue = 0;
  }
  SfbMscBuildCsw (&Csw, Context->Cbw.Tag, Residue, Context->BotStatus);
  for (Index = 0; Index < sizeof (Csw); Index++) {
    ((UINT8 *)Context->TxBuffer)[Index] = ((CONST UINT8 *)&Csw)[Index];
  }
  Context->State = SfbMscStateCsw;
  return Context->Usb->Send (SFB_MSC_EP_IN, sizeof (Csw), Context->TxBuffer);
}

STATIC
EFI_STATUS
SfbMscQueueReadChunk (IN OUT SFB_MSC_CONTEXT *Context)
{
  UINT32  MaxBlocks;
  UINT32  Blocks;
  UINTN   Bytes;
  EFI_STATUS Status;

  if (Context->BlocksLeft == 0) {
    return SfbMscQueueCsw (Context);
  }
  MaxBlocks = SFB_MSC_TRANSFER_BYTES / Context->Response.BlockSize;
  if (MaxBlocks == 0) {
    return EFI_BAD_BUFFER_SIZE;
  }
  Blocks = (Context->BlocksLeft < MaxBlocks) ? Context->BlocksLeft : MaxBlocks;
  Bytes = (UINTN)Blocks * Context->Response.BlockSize;
  Status = Context->Luns[Context->Cbw.Lun].BlockIo->ReadBlocks (
             Context->Luns[Context->Cbw.Lun].BlockIo,
             Context->Luns[Context->Cbw.Lun].BlockIo->Media->MediaId,
             Context->CurrentLba, Bytes, Context->TxBuffer);
  if (EFI_ERROR (Status)) {
    SfbMscScsiSetSense (&Context->Sense[Context->Cbw.Lun],
                        SFB_MSC_SENSE_MEDIUM_ERROR, 0x11, 0);
    Context->BotStatus = SFB_MSC_BOT_FAIL;
    return SfbMscQueueCsw (Context);
  }
  Context->ChunkBytes = (UINT32)Bytes;
  Context->State = SfbMscStateDataIn;
  return Context->Usb->Send (SFB_MSC_EP_IN, Bytes, Context->TxBuffer);
}

STATIC
EFI_STATUS
SfbMscQueueWriteChunk (IN OUT SFB_MSC_CONTEXT *Context)
{
  UINT32 MaxBlocks;
  UINT32 Blocks;

  if (Context->BlocksLeft == 0) {
    return SfbMscQueueCsw (Context);
  }
  MaxBlocks = SFB_MSC_TRANSFER_BYTES / Context->Response.BlockSize;
  if (MaxBlocks == 0) {
    return EFI_BAD_BUFFER_SIZE;
  }
  Blocks = (Context->BlocksLeft < MaxBlocks) ? Context->BlocksLeft : MaxBlocks;
  Context->ChunkBytes = Blocks * Context->Response.BlockSize;
  Context->State = SfbMscStateDataOut;
  return Context->Usb->Send (SFB_MSC_EP_OUT, Context->ChunkBytes,
                             Context->RxBuffer);
}

STATIC
EFI_STATUS
SfbMscBeginCommand (IN OUT SFB_MSC_CONTEXT *Context)
{
  EFI_STATUS Status;
  UINT8      Direction;
  UINT32     Expected;
  UINTN      Index;

  Context->CbwCount++;

  Status = SfbMscScsiCommand (Context->Cbw.Cdb, Context->Cbw.CdbLength,
                             Context->Luns, Context->LunCount, Context->Cbw.Lun,
                             &Context->Sense[Context->Cbw.Lun],
                             &Context->Response);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Context->HostLength = Context->Cbw.DataTransferLength;
  Context->Transferred = 0;
  Context->BotStatus = (Context->Response.ScsiStatus ==
                        SFB_MSC_SCSI_CHECK_CONDITION) ? SFB_MSC_BOT_FAIL :
                        SFB_MSC_BOT_OK;
  Context->BlocksLeft = Context->Response.Blocks;
  Context->CurrentLba = Context->Response.Lba;

  if (Context->Response.Eject) {
    Context->Ejected[Context->Cbw.Lun] = TRUE;
  }
  if (Context->Response.Flush && Context->Luns[Context->Cbw.Lun].BlockIo != NULL &&
      Context->Luns[Context->Cbw.Lun].BlockIo->FlushBlocks != NULL) {
    Status = Context->Luns[Context->Cbw.Lun].BlockIo->FlushBlocks (
               Context->Luns[Context->Cbw.Lun].BlockIo);
    if (EFI_ERROR (Status)) {
      SfbMscScsiSetSense (&Context->Sense[Context->Cbw.Lun],
                          SFB_MSC_SENSE_MEDIUM_ERROR, 0x0C, 0);
      Context->BotStatus = SFB_MSC_BOT_FAIL;
    }
  }

  if (Context->BotStatus != SFB_MSC_BOT_OK ||
      Context->Response.DataDirection == SFB_MSC_SCSI_DIR_NONE) {
    return SfbMscQueueCsw (Context);
  }

  Direction = (Context->Cbw.Flags & 0x80) != 0 ?
              SFB_MSC_SCSI_DIR_IN : SFB_MSC_SCSI_DIR_OUT;
  Expected = (Context->Response.Blocks != 0) ? Context->Response.DataLength :
             Context->Response.DataLength;
  if (Direction != Context->Response.DataDirection ||
      Context->HostLength != Expected) {
    Context->BotStatus = SFB_MSC_BOT_PHASE;
    return SfbMscQueueCsw (Context);
  }

  if (Context->Response.Blocks != 0) {
    if (Context->Response.DataDirection == SFB_MSC_SCSI_DIR_IN) {
      return SfbMscQueueReadChunk (Context);
    }
    return SfbMscQueueWriteChunk (Context);
  }

  for (Index = 0; Index < Context->Response.DataLength; Index++) {
    ((UINT8 *)Context->TxBuffer)[Index] = Context->Response.Data[Index];
  }
  Context->ChunkBytes = Context->Response.DataLength;
  Context->State = SfbMscStateDataIn;
  return Context->Usb->Send (SFB_MSC_EP_IN, Context->ChunkBytes,
                             Context->TxBuffer);
}

STATIC
EFI_STATUS
SfbMscTransferComplete (
  IN OUT SFB_MSC_CONTEXT             *Context,
  IN CONST USB_DEVICE_TRANSFER_OUTCOME *Outcome
  )
{
  EFI_STATUS Status;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  UINTN                  Lun;

  if (Outcome->Status != UsbDeviceTransferStatusCompleteOK) {
    return EFI_ABORTED;
  }
  Lun = Context->Cbw.Lun;
  BlockIo = Context->Luns[Lun].BlockIo;

  if (Context->State == SfbMscStateCbw) {
    Status = SfbMscParseCbw (Context->RxBuffer, Outcome->BytesCompleted,
                             &Context->Cbw, Context->LunCount);
    if (EFI_ERROR (Status)) {
      Context->Usb->SetEndpointStallState (SFB_MSC_EP_OUT, TRUE);
      Context->Usb->SetEndpointStallState (SFB_MSC_EP_IN, TRUE);
      Context->Usb->SetEndpointStallState (SFB_MSC_EP_OUT, FALSE);
      Context->Usb->SetEndpointStallState (SFB_MSC_EP_IN, FALSE);
      return SfbMscQueueCbw (Context);
    }
    return SfbMscBeginCommand (Context);
  }

  if ((Context->State == SfbMscStateDataIn ||
       Context->State == SfbMscStateDataOut) &&
      Outcome->BytesCompleted != Context->ChunkBytes) {
    Context->BotStatus = SFB_MSC_BOT_PHASE;
    return SfbMscQueueCsw (Context);
  }

  if (Context->State == SfbMscStateDataIn) {
    Context->Transferred += Context->ChunkBytes;
    if (Context->BlocksLeft != 0) {
      Context->BlocksLeft -= Context->ChunkBytes / Context->Response.BlockSize;
      Context->CurrentLba += Context->ChunkBytes / Context->Response.BlockSize;
      return SfbMscQueueReadChunk (Context);
    }
    return SfbMscQueueCsw (Context);
  }

  if (Context->State == SfbMscStateDataOut) {
    Status = BlockIo->WriteBlocks (BlockIo, BlockIo->Media->MediaId,
                                   Context->CurrentLba, Context->ChunkBytes,
                                   Context->RxBuffer);
    if (EFI_ERROR (Status)) {
      SfbMscScsiSetSense (&Context->Sense[Lun], SFB_MSC_SENSE_MEDIUM_ERROR,
                          0x0C, 0);
      Context->BotStatus = SFB_MSC_BOT_FAIL;
      return SfbMscQueueCsw (Context);
    }
    Context->Transferred += Context->ChunkBytes;
    Context->BlocksLeft -= Context->ChunkBytes / Context->Response.BlockSize;
    Context->CurrentLba += Context->ChunkBytes / Context->Response.BlockSize;
    return SfbMscQueueWriteChunk (Context);
  }

  if (Context->State == SfbMscStateCsw) {
    if (SfbMscAllEjected (Context)) {
      Context->Disconnected = TRUE;
      return EFI_SUCCESS;
    }
    return SfbMscQueueCbw (Context);
  }

  return EFI_DEVICE_ERROR;
}

EFI_STATUS
SfbMscRun (IN CONST SFB_MSC_LUN *Luns, IN UINTN LunCount)
{
  EFI_STATUS                  Status;
  EFI_USB_DEVICE_PROTOCOL    *Usb = NULL;
  USB_DEVICE_DESCRIPTOR_SET   DescriptorSet;
  SFB_MSC_CONTEXT              Context;
  USB_DEVICE_EVENT             Event;
  USB_DEVICE_EVENT_DATA        EventData;
  UINTN                        EventDataSize;
  UINTN                        Index;

  if (Luns == NULL || LunCount == 0 || LunCount > SFB_MSC_MAX_LUNS) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK msc-run luns=%u exported=0 status=%r "
            "reason=bad-lun-table\n", (UINT32)LunCount, EFI_INVALID_PARAMETER));
    return EFI_INVALID_PARAMETER;
  }
  for (Index = 0; Index < LunCount; Index++) {
    if (Luns[Index].BlockIo == NULL || Luns[Index].BlockIo->Media == NULL ||
        !Luns[Index].BlockIo->Media->MediaPresent) {
      DEBUG ((EFI_D_ERROR, "SFB: MARK msc-run luns=%u exported=%u status=%r "
              "reason=no-media\n", (UINT32)LunCount, (UINT32)Index,
              EFI_NOT_READY));
      return EFI_NOT_READY;
    }
  }

  Status = gBS->LocateProtocol (&gEfiUsbDeviceProtocolGuid, NULL,
                                (VOID **)&Usb);
  if (EFI_ERROR (Status) || Usb == NULL) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK msc-run luns=%u exported=0 status=%r "
            "reason=no-usb-protocol\n", (UINT32)LunCount, Status));
    return EFI_NOT_FOUND;
  }
  Status = SfbMscBuildDescriptorSet (&DescriptorSet);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK msc-run luns=%u exported=0 status=%r "
            "reason=descriptors\n", (UINT32)LunCount, Status));
    return Status;
  }

  for (Index = 0; Index < sizeof (Context); Index++) {
    ((UINT8 *)&Context)[Index] = 0;
  }
  Context.Usb = Usb;
  Context.Luns = Luns;
  Context.LunCount = LunCount;

  /* Nothing queued may reach the cancel test: the confirm press that opened
   * this screen is still in the queue at this point. */
  SfbMscDrainKeys ();
  Status = Usb->StartEx (&DescriptorSet);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK msc-run luns=%u exported=0 status=%r "
            "reason=start\n", (UINT32)LunCount, Status));
    return Status;
  }
  Context.Started = TRUE;
  DEBUG ((EFI_D_INFO, "SFB: MARK msc-started luns=%u\n", (UINT32)LunCount));
  Status = Usb->AllocateTransferBuffer (SFB_MSC_TRANSFER_BYTES,
                                        &Context.RxBuffer);
  if (EFI_ERROR (Status)) {
    goto Done;
  }
  Status = Usb->AllocateTransferBuffer (SFB_MSC_TRANSFER_BYTES,
                                        &Context.TxBuffer);
  if (EFI_ERROR (Status)) {
    goto Done;
  }


  while (!Context.Disconnected) {
    if (SfbMscCancelled ()) {
      Status = EFI_ABORTED;
      break;
    }
    Status = Usb->HandleEvent (&Event, &EventDataSize, &EventData);
    if (EFI_ERROR (Status)) {
      break;
    }
    if (Event == UsbDeviceEventNoEvent) {
      gBS->Stall (1000);
      continue;
    }
    if (Event == UsbDeviceEventDeviceStateChange) {
      if (EventData.DeviceState == UsbDeviceStateConnected &&
          !Context.Connected) {
        Context.Connected = TRUE;
        Status = SfbMscQueueCbw (&Context);
        if (EFI_ERROR (Status)) {
          break;
        }
      } else if (EventData.DeviceState == UsbDeviceStateDisconnected) {
        Context.Disconnected = TRUE;
      }
      continue;
    }
    if (Event == UsbDeviceEventTransferNotification &&
        USB_INDEX_TO_EP (EventData.TransferOutcome.EndpointIndex) == SFB_MSC_EP_NUM) {
      Status = SfbMscTransferComplete (&Context, &EventData.TransferOutcome);
      if (EFI_ERROR (Status)) {
        break;
      }
    }
  }

Done:
  for (Index = 0; Index < LunCount; Index++) {
    if (Luns[Index].BlockIo != NULL && Luns[Index].BlockIo->FlushBlocks != NULL) {
      Luns[Index].BlockIo->FlushBlocks (Luns[Index].BlockIo);
    }
  }
  if (Context.TxBuffer != NULL) {
    Usb->FreeTransferBuffer (Context.TxBuffer);
  }
  if (Context.RxBuffer != NULL) {
    Usb->FreeTransferBuffer (Context.RxBuffer);
  }
  if (Context.Started) {
    Usb->Stop ();
  }
  DEBUG ((EFI_D_INFO, "SFB: MARK msc-run luns=%u connected=%u cbw=%u status=%r "
          "reason=%a\n", (UINT32)LunCount, (UINT32)Context.Connected,
          (UINT32)Context.CbwCount, Status,
          Status == EFI_ABORTED ? "cancelled" :
          Context.Disconnected ? "host-gone-or-ejected" : "loop-exit"));
  return Status;
}

#endif /* SFB_HOST_BUILD */
