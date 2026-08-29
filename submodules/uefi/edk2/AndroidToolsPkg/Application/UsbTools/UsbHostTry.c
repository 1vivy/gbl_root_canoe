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

#include <Protocol/Security.h>
#include <Protocol/Security2.h>

#include <Protocol/QcomUsbConfig.h>
#include "UsbTools.h"

/*
 * The four drivers are device-extracted and unsigned, so LoadImage answers
 * Access Denied unless the security-arch authentication hooks are held open
 * for the call - the same bypass the BDS applies around SfbLoadDriver.
 * Restored immediately after, success or failure.
 */
STATIC EFI_SECURITY_ARCH_PROTOCOL             *mUtSec;
STATIC EFI_SECURITY2_ARCH_PROTOCOL            *mUtSec2;
STATIC EFI_SECURITY_FILE_AUTHENTICATION_STATE  mUtOrigSecState;
STATIC EFI_SECURITY2_FILE_AUTHENTICATION       mUtOrigSec2Auth;

STATIC
EFI_STATUS
EFIAPI
UtAllowState (
  IN CONST EFI_SECURITY_ARCH_PROTOCOL *This,
  IN UINT32                           AuthenticationStatus,
  IN CONST EFI_DEVICE_PATH_PROTOCOL   *File
  )
{
  (VOID)This;
  (VOID)AuthenticationStatus;
  (VOID)File;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
UtAllowAuth (
  IN CONST EFI_SECURITY2_ARCH_PROTOCOL *This,
  IN CONST EFI_DEVICE_PATH_PROTOCOL    *DevicePath,
  IN VOID                              *FileBuffer,
  IN UINTN                             FileSize,
  IN BOOLEAN                           BootPolicy
  )
{
  (VOID)This;
  (VOID)DevicePath;
  (VOID)FileBuffer;
  (VOID)FileSize;
  (VOID)BootPolicy;
  return EFI_SUCCESS;
}

STATIC
VOID
UtRestoreSecurity (VOID)
{
  if (mUtSec != NULL &&
      mUtSec->FileAuthenticationState == UtAllowState) {
    mUtSec->FileAuthenticationState = mUtOrigSecState;
  }
  if (mUtSec2 != NULL &&
      mUtSec2->FileAuthentication == UtAllowAuth) {
    mUtSec2->FileAuthentication = mUtOrigSec2Auth;
  }
  mUtSec = NULL;
  mUtSec2 = NULL;
  mUtOrigSecState = NULL;
  mUtOrigSec2Auth = NULL;
}

STATIC
VOID
UtBypassSecurity (VOID)
{
  UtRestoreSecurity ();
  if (!EFI_ERROR (gBS->LocateProtocol (&gEfiSecurityArchProtocolGuid, NULL,
                                       (VOID **)&mUtSec)) && mUtSec != NULL) {
    mUtOrigSecState = mUtSec->FileAuthenticationState;
    mUtSec->FileAuthenticationState = UtAllowState;
  }
  if (!EFI_ERROR (gBS->LocateProtocol (&gEfiSecurity2ArchProtocolGuid, NULL,
                                       (VOID **)&mUtSec2)) && mUtSec2 != NULL) {
    mUtOrigSec2Auth = mUtSec2->FileAuthentication;
    mUtSec2->FileAuthentication = UtAllowAuth;
  }
}

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
  UtBypassSecurity ();
  Status = gBS->LoadImage (FALSE, ImageHandle, Path, NULL, 0, &Driver);
  UtRestoreSecurity ();
  FreePool (Path);
  if (Status == EFI_NOT_FOUND) {
    UnicodeSPrint (Full, sizeof (Full), L"\\usbhost\\%s", Name);
    Path = FileDevicePath (Loaded->DeviceHandle, Full);
    if (Path == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    UtBypassSecurity ();
    Status = gBS->LoadImage (FALSE, ImageHandle, Path, NULL, 0, &Driver);
    UtRestoreSecurity ();
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

  /* An empty before-set would mark every existing handle as new and offer
   * the stale peripheral handle to the shim - the double-bind this whole
   * function exists to prevent. */
  if (Before == NULL && BeforeCount == 0) {
    UtStep (L"bind skipped: handle snapshot failed");
    return;
  }

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
  UINT32                    Limit     = 1;
  UINT32                    Index;
  UINT32                    Vbus;
  UINTN                     SfsBefore;
  UINTN                     Step;
  EFI_STATUS                Status;
  EFI_STATUS                Restore  = EFI_SUCCESS;
  BOOLEAN                   Found     = FALSE;
  BOOLEAN                   Started   = FALSE;
  BOOLEAN                   Toggled   = FALSE;

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
  /*
   * Cap the scan at the vendor's own core count: querying cores past it is
   * an unproven call on this device class - the same class of query that
   * froze the census on a stopped core. Only core 0 is BDS-proven, so a
   * missing count reads as "core 0 only", never as "all six".
   */
  {
    UINT8  Reported = 0;

    if (Cfg->GetCoreCount != NULL &&
        !EFI_ERROR (Cfg->GetCoreCount (Cfg, &Reported)) &&
        Reported > 0 && Reported <= QCOM_USB_CORE_MAX_NUM) {
      Limit = Reported;
    }
  }
  for (Index = 0; Index < Limit && !Found; Index++) {
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

  /*
   * The vendor's own mode switch is the primary rung. Measured on this
   * device: the direct StartController(XHCI) returns Success and still
   * leaves the new handle in DEVICE mode (the static-config override), while
   * ToggleUsbMode stops the core, pins the static config to XHCI and lets
   * the vendor complete the host start ASYNCHRONOUSLY - Usb2Hc and PciIo
   * appeared seconds later, not at return. So the toggle is followed by a
   * settle poll, not an immediate bind.
   */
  BeforeCount = UtConfigHandles (&Before);
  if (Cfg->ToggleUsbMode != NULL) {
    UtStep (L"state before toggle: mode=0x%x", Cfg->ModeType);
    Status = Cfg->ToggleUsbMode (Cfg, Capable);
    UtStep (L"ToggleUsbMode(core %u): %r", Capable, Status);
    if (!EFI_ERROR (Status)) {
      Toggled = TRUE;
      for (Step = 0;
           Step < 16 &&
           UtCountByProtocol (&gEfiUsb2HcProtocolGuid) == 0;
           Step++) {
        gBS->Stall (UT_ENUM_STEP_US);
      }
      UtStep (L"host settle: usb2hc=%u pciio=%u after %Lu ms",
              (UINT32)UtCountByProtocol (&gEfiUsb2HcProtocolGuid),
              (UINT32)UtCountByProtocol (&gEfiPciIoProtocolGuid),
              (UINT64)(Step * (UT_ENUM_STEP_US / 1000u)));
      UtBindNewHandles (Before, BeforeCount);
    }
  } else {
    /* No toggle on this build: the direct start is the only lever. It stops
     * device mode itself and creates the host handle. */
    Status = Cfg->StartController (Cfg, Capable, QCOM_USB_HOST_MODE_XHCI);
    UtStep (L"StartController(core %u, XHCI): %r", Capable, Status);
    Started = !EFI_ERROR (Status);
    if (!Started) {
      goto Out;
    }
    UtStep (L"started; offering only the new handle(s) to bindings");
    UtBindNewHandles (Before, BeforeCount);
  }

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

  /*
   * Always hand the core back; "nothing to do" is never an acceptable end
   * state. A stopped (INVALID) core is just as broken for the gadget stack
   * as a host-mode one - the mass-storage export failed exactly that way.
   * The toggle-back is asynchronous too, so it gets its own settle poll,
   * and a direct device-mode start covers whatever the toggle left behind.
   */
  if (Toggled) {
    Restore = Cfg->ToggleUsbMode (Cfg, Capable);
    UtStep (L"restore via ToggleUsbMode: %r", Restore);
    for (Step = 0;
         Step < 16 &&
         UtCountByProtocol (&gEfiUsb2HcProtocolGuid) != 0;
         Step++) {
      gBS->Stall (UT_ENUM_STEP_US);
    }
    UtStep (L"restore settle: usb2hc=%u after %Lu ms",
            (UINT32)UtCountByProtocol (&gEfiUsb2HcProtocolGuid),
            (UINT64)(Step * (UT_ENUM_STEP_US / 1000u)));
  }
  if (UtCountByProtocol (&gEfiUsb2HcProtocolGuid) != 0 ||
      Cfg->ModeType != QCOM_USB_DEVICE_MODE_SS) {
    Restore = Cfg->StartController (Cfg, Capable, QCOM_USB_DEVICE_MODE_SS);
    UtStep (L"device-mode start: %r (state now mode=0x%x)",
            Restore, Cfg->ModeType);
  } else {
    UtStep (L"restore complete: mode=0x%x", Cfg->ModeType);
  }

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
