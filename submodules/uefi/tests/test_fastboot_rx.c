/* Exercise the real USB event loop with a transport that rejects overlapping
 * receives of the one shared command buffer. */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#undef NULL
#include <Uefi.h>
#include <Protocol/EFIUsbDevice.h>
#include "../edk2/QcomModulePkg/Library/FastbootLib/FastbootMain.h"
#include "../edk2/QcomModulePkg/Library/FastbootLib/FastbootCmds.h"

/* EDK2's AutoGen.h supplies this declaration in the firmware build. */
extern EFI_GUID gQcomTokenSpaceGuid;
#include "../edk2/QcomModulePkg/Library/FastbootLib/FastbootMain.c"

static EFI_USB_DEVICE_PROTOCOL Usb;
static char Rx[512];
static void *Queued;
static unsigned Sends, Commands;
static USB_DEVICE_EVENT NextEvent;
static USB_DEVICE_EVENT_DATA Payload;

static EFI_STATUS Send (UINT8 Endpoint, UINTN Size, VOID *Buffer)
{
  assert(Endpoint == 1 && Size == 511 && Buffer == Rx);
  assert(Queued == NULL);
  Queued = Buffer;
  Sends++;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI HandleEvent (USB_DEVICE_EVENT *Message,
                                      UINTN *Size, USB_DEVICE_EVENT_DATA *Data)
{
  *Message = NextEvent;
  *Size = sizeof(Payload);
  *Data = Payload;
  NextEvent = UsbDeviceEventNoEvent;
  return EFI_SUCCESS;
}

ANDROID_FASTBOOT_STATE FastbootCurrentState (VOID) { return ExpectCmdState; }
UINTN GetXfrSize (VOID) { return 511; }
VOID *FastbootNextDataBuffer (VOID) { assert(0); return NULL; }
VOID *FastbootDloadBuffer (VOID) { assert(0); return NULL; }
VOID DataReady (UINT64 Size, VOID *Data)
{
  const char *Expected[] = {"getvar:product", "download:00300000", "reboot"};
  assert(Commands < sizeof(Expected) / sizeof(Expected[0]));
  assert(Size == strlen(Expected[Commands]));
  assert(Data == Rx);
  assert(memcmp(Data, Expected[Commands], Size) == 0);
  Commands++;
}

static void Dispatch (USB_DEVICE_EVENT Message)
{
  NextEvent = Message;
  assert(HandleUsbEvents() == EFI_SUCCESS);
}

int main (void)
{
  const char *CommandsToSend[] = {"getvar:product", "download:00300000", "reboot"};
  unsigned Index;
  Usb.Send = Send;
  Usb.HandleEvent = HandleEvent;
  GetFastbootDeviceData()->UsbDeviceProtocol = &Usb;
  GetFastbootDeviceData()->gRxBuffer = Rx;

  Payload.DeviceState = UsbDeviceStateConnected;
  Dispatch(UsbDeviceEventDeviceStateChange);
  Dispatch(UsbDeviceEventDeviceStateChange); /* reconnect and Connected may both seed */
  assert(Sends == 1);
  for (Index = 0; Index < 3; Index++) {
    assert(Queued == Rx);
    memcpy(Rx, CommandsToSend[Index], strlen(CommandsToSend[Index]));
    Queued = NULL; /* transport has finished the host-to-device transfer */
    Payload.TransferOutcome.Status = UsbDeviceTransferStatusCompleteOK;
    Payload.TransferOutcome.EndpointIndex = 1;
    Payload.TransferOutcome.BytesCompleted = strlen(CommandsToSend[Index]);
    Dispatch(UsbDeviceEventTransferNotification);
    /* The response completion seeds exactly one request for the next command. */
    Payload.TransferOutcome.EndpointIndex = 0x81;
    Dispatch(UsbDeviceEventTransferNotification);
    Payload.DeviceState = UsbDeviceStateConnected;
    Dispatch(UsbDeviceEventDeviceStateChange);
    assert(Sends == Index + 2);
  }
  assert(Commands == 3);
  puts("PASS fastboot RX: duplicate connection seeds, sequential getvar/download/reboot");
  return 0;
}
