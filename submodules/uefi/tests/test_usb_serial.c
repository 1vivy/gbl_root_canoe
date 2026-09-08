#include <assert.h>
#include <stdio.h>
#include <string.h>
#undef NULL
#include <Uefi.h>
#include <Protocol/EFIUsbfnIo.h>
#include "../edk2/QcomModulePkg/Library/FastbootLib/UsbSerial.h"

static EFI_USBFN_IO_PROTOCOL Usb;
static EFI_BOOT_SERVICES Services;
EFI_BOOT_SERVICES *gBS = &Services;
EFI_GUID gEfiUsbfnIoProtocolGuid;
static EFI_STATUS LocateStatus, ReadStatus;
static CHAR16 Serial[64];
static UINTN SerialBytes;
static EFI_STATUS EFIAPI Locate(EFI_GUID *Guid, VOID *Registration, VOID **Output) {
  (void)Guid; (void)Registration;
  *Output = &Usb;
  return LocateStatus;
}
static EFI_STATUS EFIAPI ReadInfo(EFI_USBFN_IO_PROTOCOL *This, EFI_USBFN_DEVICE_INFO_ID Id, UINTN *Bytes, VOID *Buffer) {
  (void)This;
  assert(Id == EfiUsbDeviceInfoSerialNumber);
  assert(*Bytes == sizeof(Serial));
  memcpy(Buffer, Serial, sizeof(Serial));
  *Bytes = SerialBytes;
  return ReadStatus;
}
static void Set(const char *Value) {
  unsigned i;
  memset(Serial, 0, sizeof(Serial));
  for(i=0; Value[i]; i++) Serial[i] = Value[i];
  SerialBytes = i * sizeof(CHAR16);
}
int main(void) {
  char Output[31];
  Services.LocateProtocol = Locate;
  Usb.GetDeviceInfo = ReadInfo;
  Set("PHONE-123");
  assert(SfbUsbSerial(Output, sizeof(Output)) == EFI_SUCCESS);
  assert(strcmp(Output, "PHONE-123") == 0);
  SerialBytes += sizeof(CHAR16);
  assert(SfbUsbSerial(Output, sizeof(Output)) == EFI_SUCCESS);
  assert(strcmp(Output, "PHONE-123") == 0);
  assert(SfbUsbSerial(Output, 4) == EFI_BUFFER_TOO_SMALL);
  assert(Output[0] == 0);
  Set("0000000000000000"); assert(EFI_ERROR(SfbUsbSerial(Output, sizeof(Output))));
  Set("PHONE-123"); Serial[2] = 0; assert(EFI_ERROR(SfbUsbSerial(Output, sizeof(Output))));
  Set("PHONE-123"); Serial[2] = 0x1234; assert(EFI_ERROR(SfbUsbSerial(Output, sizeof(Output))));
  Set("PHONE-123"); SerialBytes++; assert(EFI_ERROR(SfbUsbSerial(Output, sizeof(Output))));
  SerialBytes = sizeof(Serial) + 2; assert(EFI_ERROR(SfbUsbSerial(Output, sizeof(Output))));
  SerialBytes = 0; assert(EFI_ERROR(SfbUsbSerial(Output, sizeof(Output))));
  LocateStatus = EFI_NOT_FOUND; assert(SfbUsbSerial(Output, sizeof(Output)) == EFI_NOT_FOUND);
  LocateStatus = EFI_SUCCESS; ReadStatus = EFI_DEVICE_ERROR;
  assert(SfbUsbSerial(Output, sizeof(Output)) == EFI_DEVICE_ERROR);
  Usb.GetDeviceInfo = NULL; assert(SfbUsbSerial(Output, sizeof(Output)) == EFI_UNSUPPORTED);
  puts("Platform USB serial: bounded and unavailable identity cases passed");
  return 0;
}
