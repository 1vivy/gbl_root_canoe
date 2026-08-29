/** @file
 *  Passive USB census for UsbTools. Read-only: every row is a handle count
 *  or a vendor query that changes no controller state.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/PciIo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/Usb2HostController.h>
#include <Protocol/UsbIo.h>

#include <Protocol/QcomUsbConfig.h>
#include "UsbTools.h"

STATIC
UINTN
UtCountByProtocol (IN EFI_GUID *Protocol)
{
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count    = 0;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, Protocol, NULL,
                                          &Count, &Handles))) {
    return 0;
  }
  if (Handles != NULL) {
    FreePool (Handles);
  }
  return Count;
}

STATIC
VOID
UtCensusInstance (
  IN OUT AT_REPORT              *Report,
  IN UINTN                      Index,
  IN QCOM_USB_CONFIG_PROTOCOL   *Cfg
  )
{
  UINT32  Modes;
  UINT8   Count;

  AtReportAdd (Report, L"instance %u: rev=0x%lx core=%u mode=0x%x always=%u",
               (UINT32)Index, Cfg->Revision, Cfg->CoreNum, Cfg->ModeType,
               (UINT32)Cfg->AlwaysConnected);

  AtReportAdd (Report,
               L"  vtable: start=%c stop=%c cfgusb=%c toggle=%c "
               L"corecount=%c supmode=%c",
               (Cfg->StartController != NULL) ? L'Y' : L'-',
               (Cfg->StopController != NULL) ? L'Y' : L'-',
               (Cfg->ConfigUsb != NULL) ? L'Y' : L'-',
               (Cfg->ToggleUsbMode != NULL) ? L'Y' : L'-',
               (Cfg->GetCoreCount != NULL) ? L'Y' : L'-',
               (Cfg->GetSupUsbMode != NULL) ? L'Y' : L'-');
  AtReportAdd (Report,
               L"  vtable: vbusstat=%c vbuson=%c maxhost=%c setcoremode=%c",
               (Cfg->GetUsbVbusStatus != NULL) ? L'Y' : L'-',
               (Cfg->UsbEnableVbus != NULL) ? L'Y' : L'-',
               (Cfg->GetUsbMaxHostCoreNum != NULL) ? L'Y' : L'-',
               (Cfg->Revision >= QCOM_USB_CFG_REVISION_3 &&
                Cfg->SetUsbCoreMode != NULL) ? L'Y' : L'-');

  /*
   * Presence is reported for every member, but only calls the BDS has
   * already exercised on this device class are made here: GetCoreCount,
   * and GetSupUsbMode on a core whose mode is valid. Querying a core in
   * USB_INVALID_MODE - and GetUsbMaxHostCoreNum at all - froze the device
   * in this census; the vendor's port query does not tolerate a stopped
   * core.
   */
  if (Cfg->GetCoreCount != NULL &&
      !EFI_ERROR (Cfg->GetCoreCount (Cfg, &Count))) {
    AtReportAdd (Report, L"  GetCoreCount = %u", Count);
  }
  if (Cfg->GetSupUsbMode != NULL) {
    if (Cfg->ModeType != QCOM_USB_INVALID_MODE &&
        !EFI_ERROR (Cfg->GetSupUsbMode (Cfg, Cfg->CoreNum, &Modes))) {
      AtReportAdd (Report,
                   L"  GetSupUsbMode(core %u) = 0x%x (host=%c device=%c drd=%c)",
                   Cfg->CoreNum, Modes,
                   (Modes & QCOM_USB_HOST_MODE) ? L'Y' : L'-',
                   (Modes & QCOM_USB_DEVICE_MODE) ? L'Y' : L'-',
                   (Modes & QCOM_USB_DUAL_ROLE_MODE) ? L'Y' : L'-');
    } else if (Cfg->ModeType == QCOM_USB_INVALID_MODE) {
      AtReportAdd (Report, L"  GetSupUsbMode(core %u) skipped: core stopped",
                   Cfg->CoreNum);
    } else {
      AtReportAdd (Report, L"  GetSupUsbMode(core %u) failed", Cfg->CoreNum);
    }
  }
}

EFI_STATUS
UtBuildCensusReport (OUT AT_REPORT *Report)
{
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count    = 0;
  UINTN       Index;
  EFI_STATUS  Status;

  Status = AtReportInit (Report, UT_CENSUS_ROWS);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  /* The report is drawn only after it is fully built, so a vendor call that
   * hangs would leave the screen showing the previous menu. Print a live
   * line per stage: a freeze names itself. */
  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"[census: counts]\r\n");
  gBS->Stall (120 * 1000);

  AtReportAdd (Report, L"usbconfig=%u usb2hc=%u pciio=%u usbio=%u",
               (UINT32)UtCountByProtocol (&gQcomUsbConfigProtocolGuid),
               (UINT32)UtCountByProtocol (&gEfiUsb2HcProtocolGuid),
               (UINT32)UtCountByProtocol (&gEfiPciIoProtocolGuid),
               (UINT32)UtCountByProtocol (&gEfiUsbIoProtocolGuid));
  AtReportAdd (Report, L"blkio=%u sfs=%u driverbinding=%u usbfn=%u",
               (UINT32)UtCountByProtocol (&gEfiBlockIoProtocolGuid),
               (UINT32)UtCountByProtocol (&gEfiSimpleFileSystemProtocolGuid),
               (UINT32)UtCountByProtocol (&gEfiDriverBindingProtocolGuid),
               (UINT32)UtCountByProtocol (&gEfiUsbfnIoProtocolGuid));

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol,
                                          &gQcomUsbConfigProtocolGuid,
                                          NULL, &Count, &Handles)) ||
      Handles == NULL) {
    AtReportAdd (Report, L"no UsbConfig instances: resident firmware carries "
                         L"no UsbConfigDxe; host mode is unreachable");
    return EFI_SUCCESS;
  }

  for (Index = 0; Index < Count; Index++) {
    QCOM_USB_CONFIG_PROTOCOL  *Cfg = NULL;

    Print (L"[census: instance %Lu]\r\n", (UINT64)Index);
    gBS->Stall (120 * 1000);

    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index],
                                        &gQcomUsbConfigProtocolGuid,
                                        (VOID **)&Cfg)) ||
        Cfg == NULL) {
      AtReportAdd (Report, L"instance %u: HandleProtocol failed", (UINT32)Index);
      continue;
    }
    UtCensusInstance (Report, Index, Cfg);
  }

  FreePool (Handles);
  return EFI_SUCCESS;
}
