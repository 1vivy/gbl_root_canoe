/** @file
 *  The confirmed USB host-mode attempt for UsbTools.
 *
 *  Mirrors the BDS acquire step for step, with every stage written to the
 *  transcript and printed live with a dwell, so a fault leaves the stage
 *  name on the display - the property that localised every crash in the
 *  menu-driven version of this path.
 *
 *  Vendor-ordering invariants, hard-won on this device class and not
 *  negotiable:
 *   - drive a core through the UsbConfig instance bound to it; the vendor
 *     reads the core number from the instance, never from the CoreNum
 *     argument;
 *   - StartController stops the previous mode itself; never call
 *     StopController from the outside;
 *   - offer only handles the start created to the driver bindings - the
 *     stale peripheral handle shares the new modeType and double-binds the
 *     XHCI PCI-emulation shim.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/PciIo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/Usb2HostController.h>
#include <Protocol/UsbIo.h>

#include <Protocol/QcomUsbConfig.h>
#include "UsbTools.h"

AT_REPORT  mUtAttemptReport;
BOOLEAN    mUtAttemptRan = FALSE;

STATIC CONST CHAR16 *CONST mUtDriverStack[] = {
  L"XhciPciEmulation.efi",
  L"XhciDxe.efi",
  L"UsbBusDxe.efi",
  L"UsbMassStorageDxe.efi"
};

#define UT_ENUM_STEPS     12u
#define UT_ENUM_STEP_US   (250u * 1000u)

/* One transcript row and one screen line with a dwell. A fault after this
 * point leaves the stage on the display, which is the whole reason the
 * attempt prints as it goes instead of reporting at the end. */
STATIC
VOID
UtStep (IN CONST CHAR16 *Format, ...)
{
  VA_LIST   Args;
  CHAR16    Row[AT_ROW_CHARS];
  CHAR16    *Target;

  VA_START (Args, Format);
  UnicodeVSPrint (Row, sizeof (Row), Format, Args);
  VA_END (Args);

  Target = AtReportNextRow (&mUtAttemptReport);
  if (Target != NULL) {
    StrCpyS (Target, AT_ROW_CHARS, Row);
  }
  Print (L"%s\r\n", Row);
  gBS->Stall (120 * 1000);
}

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
UINTN
UtConfigHandles (OUT EFI_HANDLE **Handles)
{
  UINTN  Count = 0;

  *Handles = NULL;
  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol,
                                          &gQcomUsbConfigProtocolGuid,
                                          NULL, &Count, Handles))) {
    *Handles = NULL;
    return 0;
  }
  return Count;
}

STATIC
QCOM_USB_CONFIG_PROTOCOL *
UtConfigForCore (IN UINT32 Core)
{
  EFI_HANDLE                *Handles = NULL;
  UINTN                     Count;
  UINTN                     Index;
  QCOM_USB_CONFIG_PROTOCOL  *Found = NULL;

  Count = UtConfigHandles (&Handles);
  for (Index = 0; Index < Count && Found == NULL; Index++) {
    QCOM_USB_CONFIG_PROTOCOL  *Cfg = NULL;

    if (!EFI_ERROR (gBS->HandleProtocol (Handles[Index],
                                         &gQcomUsbConfigProtocolGuid,
                                         (VOID **)&Cfg)) &&
        Cfg != NULL && Cfg->CoreNum == Core) {
      Found = Cfg;
    }
  }
  if (Handles != NULL) {
    FreePool (Handles);
  }
  return Found;
}

/*
 * Load and start one driver image from the boot root. The tool lives at
 * <root>\efisp\tools\UsbTools.efi and the drivers at <root>\efisp\usbhost\;
 * the plain usbhost\ path is the fallback for a stick-rooted layout.
 */
STATIC
EFI_STATUS
UtLoadDriver (IN EFI_HANDLE ImageHandle, IN CONST CHAR16 *Name)
{
  EFI_LOADED_IMAGE_PROTOCOL   *Loaded = NULL;
  EFI_DEVICE_PATH_PROTOCOL    *Path   = NULL;
  EFI_HANDLE                  Driver  = NULL;
  EFI_STATUS                  Status;
  CHAR16                      Full[96];

  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid,
                                (VOID **)&Loaded);
  if (EFI_ERROR (Status) || Loaded == NULL) {
    return Status;
  }

  UnicodeSPrint (Full, sizeof (Full), L"\\efisp\\usbhost\\%s", Name);
  Path = FileDevicePath (Loaded->DeviceHandle, Full);
  if (Path == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  Status = gBS->LoadImage (FALSE, ImageHandle, Path, NULL, 0, &Driver);
  FreePool (Path);
  if (Status == EFI_NOT_FOUND) {
    UnicodeSPrint (Full, sizeof (Full), L"\\usbhost\\%s", Name);
    Path = FileDevicePath (Loaded->DeviceHandle, Full);
    if (Path == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    Status = gBS->LoadImage (FALSE, ImageHandle, Path, NULL, 0, &Driver);
    FreePool (Path);
  }
  if (EFI_ERROR (Status)) {
    return Status;
  }
  return gBS->StartImage (Driver, NULL, NULL);
}

/*
 * Offer only the handles the start created to the driver bindings, and
 * report each with the fields the shim's Supported() is about to test:
 * coreNum < USB_CORE_MAX_NUM and modeType == XHCI. A pass over every
 * UsbConfig handle is never acceptable - see the file header.
 */
STATIC
VOID
UtBindNewHandles (IN EFI_HANDLE *Before, IN UINTN BeforeCount)
{
  EFI_HANDLE  *After = NULL;
  UINTN       AfterCount;
  UINTN       Outer;
  UINTN       Inner;
  BOOLEAN     Known;

  AfterCount = UtConfigHandles (&After);
  for (Outer = 0; Outer < AfterCount; Outer++) {
    Known = FALSE;
    for (Inner = 0; Inner < BeforeCount; Inner++) {
      if (After[Outer] == Before[Inner]) {
        Known = TRUE;
        break;
      }
    }
    if (Known) {
      continue;
    }

    {
      QCOM_USB_CONFIG_PROTOCOL  *Cfg = NULL;

      if (!EFI_ERROR (gBS->HandleProtocol (After[Outer],
                                           &gQcomUsbConfigProtocolGuid,
                                           (VOID **)&Cfg)) &&
          Cfg != NULL) {
        UtStep (L"new handle: core=%u mode=0x%x (shim gate wants mode=0x%x)",
                Cfg->CoreNum, Cfg->ModeType, QCOM_USB_HOST_MODE_XHCI);
      } else {
        UtStep (L"new handle: fields unreadable");
      }
      gBS->ConnectController (After[Outer], NULL, NULL, TRUE);
      UtStep (L"connect: pciio=%u usb2hc=%u",
              (UINT32)UtCountByProtocol (&gEfiPciIoProtocolGuid),
              (UINT32)UtCountByProtocol (&gEfiUsb2HcProtocolGuid));
    }
  }
  if (After != NULL) {
    FreePool (After);
  }
}

EFI_STATUS
UtRunHostAttempt (IN EFI_HANDLE ImageHandle)
{
  QCOM_USB_CONFIG_PROTOCOL  *Cfg;
  EFI_HANDLE                *Before   = NULL;
  UINTN                     BeforeCount;
  EFI_HANDLE                *Handles  = NULL;
  UINTN                     HandleCount;
  UINT32                    Capable   = QCOM_USB_CORE_0;
  UINT32                    Index;
  UINT32                    Vbus;
  UINTN                     SfsBefore;
  UINTN                     Step;
  EFI_STATUS                Status;
  EFI_STATUS                Restore  = EFI_SUCCESS;
  BOOLEAN                   Found     = FALSE;

  AtReportFree (&mUtAttemptReport);
  Status = AtReportInit (&mUtAttemptReport, UT_ATTEMPT_ROWS);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  mUtAttemptRan = TRUE;

  AtUiBeginScreen (L"USB Host Mode Attempt", L"Writing controller state");

  /* Capability gate: never write to a core that cannot do this. */
  HandleCount = UtConfigHandles (&Handles);
  if (HandleCount == 0) {
    UtStep (L"no UsbConfig instances; host mode unreachable on this target");
    goto Out;
  }

  Cfg = UtConfigForCore (QCOM_USB_CORE_0);
  if (Cfg == NULL || Cfg->GetSupUsbMode == NULL) {
    UtStep (L"core-0 instance missing or lacks GetSupUsbMode");
    goto Out;
  }
  for (Index = 0; Index < QCOM_USB_CORE_MAX_NUM && !Found; Index++) {
    UINT32      Modes = 0;
    EFI_STATUS  Query = Cfg->GetSupUsbMode (Cfg, Index, &Modes);

    UtStep (L"capability core=%u status=%r modes=0x%x", Index, Query, Modes);
    if (!EFI_ERROR (Query) &&
        (Modes & (QCOM_USB_HOST_MODE | QCOM_USB_DUAL_ROLE_MODE)) != 0) {
      Capable = Index;
      Found   = TRUE;
    }
  }
  if (!Found) {
    UtStep (L"no core reports host capability; nothing was written");
    goto Out;
  }

  /* The core's own instance, not LocateProtocol's first hit. */
  Cfg = UtConfigForCore (Capable);
  if (Cfg == NULL || Cfg->StartController == NULL) {
    UtStep (L"no UsbConfig instance bound to core %u", Capable);
    goto Out;
  }
  UtStep (L"core %u via its own instance (rev 0x%lx)", Capable, Cfg->Revision);

  /* Driver stack from the boot root, in dependency order. */
  for (Index = 0; Index < ARRAY_SIZE (mUtDriverStack); Index++) {
    Status = UtLoadDriver (ImageHandle, mUtDriverStack[Index]);
    UtStep (L"driver %s: %r", mUtDriverStack[Index], Status);
  }

  /* The vendor start stops device mode itself and creates the host handle. */
  BeforeCount = UtConfigHandles (&Before);
  Status = Cfg->StartController (Cfg, Capable, QCOM_USB_HOST_MODE_XHCI);
  UtStep (L"StartController(core %u, XHCI): %r", Capable, Status);
  if (EFI_ERROR (Status)) {
    goto Out;
  }
  UtStep (L"started; offering only the new handle(s) to bindings");
  UtBindNewHandles (Before, BeforeCount);

  /* Bus power. A stick with no VBUS never enumerates. */
  if (Cfg->GetUsbVbusStatus != NULL && Cfg->UsbEnableVbus != NULL) {
    if (!EFI_ERROR (Cfg->GetUsbVbusStatus (Cfg, Capable, &Vbus))) {
      UtStep (L"vbus before: %u", Vbus);
      if (Vbus == QCOM_USB_VBUS_DISABLED) {
        Status = Cfg->UsbEnableVbus (Cfg, Capable);
        UtStep (L"UsbEnableVbus: %r", Status);
        if (!EFI_ERROR (Cfg->GetUsbVbusStatus (Cfg, Capable, &Vbus))) {
          UtStep (L"vbus after: %u", Vbus);
        }
      }
    }
  }

  /* Enumeration settle: SCSI capacity behind BlockIo is not instant. */
  SfsBefore = UtCountByProtocol (&gEfiSimpleFileSystemProtocolGuid);
  for (Step = 0; Step < UT_ENUM_STEPS; Step++) {
    gBS->Stall (UT_ENUM_STEP_US);
    if (UtCountByProtocol (&gEfiSimpleFileSystemProtocolGuid) > SfsBefore) {
      break;
    }
  }
  UtStep (L"enum wait: sfs %Lu -> %Lu after %Lu ms",
          (UINT64)SfsBefore,
          (UINT64)UtCountByProtocol (&gEfiSimpleFileSystemProtocolGuid),
          (UINT64)(Step * (UT_ENUM_STEP_US / 1000u)));

  UtStep (L"verdict: usb2hc=%u pciio=%u usbio=%u blkio=%u sfs=%u",
          (UINT32)UtCountByProtocol (&gEfiUsb2HcProtocolGuid),
          (UINT32)UtCountByProtocol (&gEfiPciIoProtocolGuid),
          (UINT32)UtCountByProtocol (&gEfiUsbIoProtocolGuid),
          (UINT32)UtCountByProtocol (&gEfiBlockIoProtocolGuid),
          (UINT32)UtCountByProtocol (&gEfiSimpleFileSystemProtocolGuid));

  /* Always hand the core back, in the vendor's order. */
  Restore = Cfg->StartController (Cfg, Capable, QCOM_USB_DEVICE_MODE_SS);
  UtStep (L"restore device mode: %r", Restore);

Out:
  if (Before != NULL) {
    FreePool (Before);
  }
  if (Handles != NULL) {
    FreePool (Handles);
  }
  AtUiEndScreen (L"Power: back");
  while (AtUiWaitForKey (0) != AtKeySelect) {
  }
  return EFI_SUCCESS;
}
