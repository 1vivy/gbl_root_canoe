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
#include <Library/IoLib.h>
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
#include "UsbPwrCtrl.h"
#include "UsbPmicSchg.h"
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

/*
 * The charger route is closed, recorded here so it is not retried.
 *
 * GlinkDxe, PmicGlinkDxe and ChargerExDxe all started and ChargerCount did
 * move 0 -> 1, but QcomChargerDxeLA - the one that runs SchgInit and fills
 * the port table - answered Device Error. Its INF locates five protocols at
 * entry under a TRUE depex; measured against the device's live census four
 * are present and gEfiDppProtocolGuid
 * (42D430C0-AE55-4A6C-A795-C63E687A1549) is absent. DppDxe is not in this
 * handset's uefi_a and nothing on the ABL path publishes it, so that driver
 * cannot start here in any load order.
 *
 * That run also left the USB gadget wedged past a reboot, needing a hard
 * power cycle to recover. Loading the stack is therefore removed: it costs
 * a device recovery and cannot reach a working port. Getting further would
 * need a stub DPP provider - which lets a charging application configure a
 * battery from defaults, a hardware risk rather than a software one - or
 * direct SPMI register writes.
 */

/*
 * Load order is dependency order. UsbPwrCtrlDxe comes first because it
 * publishes EFI_USB_PWR_CTRL_PROTOCOL, the only thing that can raise VBUS:
 * the vendor's host init asks for VBUS *off*, and XhciDxe's automatic
 * re-enable in XhcGetRootHubPortStatus is #if 0 in the vendor source, so
 * nothing turns bus power on implicitly. This DXE lives in the device's own
 * uefi_a volume and is not dispatched on the Android boot path, which is
 * why the protocol is missing from our runtime entirely.
 */
STATIC CONST CHAR16 *CONST mUtDriverStack[] = {
  L"UsbPwrCtrlDxe.efi",
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

/*
 * The vendor's on-demand USB start: signal gInitUsbControllerGuid and the
 * UsbConfigDxe callback brings the core up in whatever mode the static
 * config is pinned to. The toggle pins the mode; this completes it. The
 * pairing is the whole mode-switch protocol on this device - neither half
 * works alone.
 */
STATIC
VOID
UtInitEventNotify (IN EFI_EVENT Event, IN VOID *Context)
{
  (VOID)Event;
  (VOID)Context;
}

STATIC
VOID
UtSignalUsbInit (VOID)
{
  EFI_EVENT   Event;
  EFI_STATUS  Status;
  EFI_GUID    InitUsbControllerGuid = {
    0x1c0cffce, 0xfc8d, 0x4e44,
    { 0x8c, 0x78, 0x9c, 0x9e, 0x5b, 0x53, 0xd, 0x36 }
  };

  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                               UtInitEventNotify, NULL,
                               &InitUsbControllerGuid, &Event);
  if (EFI_ERROR (Status)) {
    UtStep (L"init-event create failed: %r", Status);
    return;
  }
  gBS->SignalEvent (Event);
  gBS->CloseEvent (Event);
}

/* TRUE once the vendor's deferred host start has materialised: a real
 * EFI_USB2_HC_PROTOCOL, not a mode field that may be a stale copy. */
STATIC
BOOLEAN
UtHostIsUp (VOID)
{
  return UtCountByProtocol (&gEfiUsb2HcProtocolGuid) != 0;
}

/*
 * Read the XHCI root-port registers directly and report them.
 *
 * This is the one fact no protocol count can give and this firmware's
 * XhciDxe will not print: whether the controller sees anything on the wire.
 * PORTSC.CCS says a device is electrically present, PORTSC.PP says the port
 * is powered. CCS=0 with a stick attached means the gap is power or role,
 * not software - which is exactly the question the vendor's own
 * "UsbPwrCtrlLib_ValidateRequest Invalid Port Index" leaves open.
 *
 * Register layout is the xHCI spec: CAPLENGTH at byte 0, HCSPARAMS1 at +4
 * (MaxPorts in bits 24-31), and the port register set at
 * operational base + 0x400 + 0x10 * port. Read-only throughout.
 */
STATIC
VOID
UtReportXhciPorts (IN QCOM_USB_CONFIG_PROTOCOL *Cfg, IN UINT32 Core)
{
  EFI_STATUS  Status;
  UINT32      CoreType = 0;
  UINTN       Base     = 0;
  UINT32      HcsParams1;
  UINT32      Ports;
  UINT32      Index;
  UINT8       CapLength;

  if (Cfg->GetUsbHostConfig == NULL || Cfg->GetCoreBaseAddr == NULL) {
    UtStep (L"xhci ports: base lookup members absent");
    return;
  }

  Status = Cfg->GetUsbHostConfig (Cfg, QCOM_USB_HOST_MODE_XHCI, Core,
                                  &CoreType);
  if (EFI_ERROR (Status)) {
    UtStep (L"xhci ports: GetUsbHostConfig %r", Status);
    return;
  }
  Status = Cfg->GetCoreBaseAddr (Cfg, CoreType, &Base);
  if (EFI_ERROR (Status) || Base == 0) {
    UtStep (L"xhci ports: GetCoreBaseAddr %r base=0x%Lx", Status,
            (UINT64)Base);
    return;
  }

  CapLength  = MmioRead8 (Base);
  HcsParams1 = MmioRead32 (Base + 0x4);
  Ports      = (HcsParams1 >> 24) & 0xFF;
  UtStep (L"xhci base=0x%Lx caplen=0x%x ports=%u", (UINT64)Base,
          (UINT32)CapLength, Ports);

  if (Ports > 8) {
    Ports = 8;
  }
  for (Index = 0; Index < Ports; Index++) {
    UINT32  Portsc = MmioRead32 (Base + CapLength + 0x400 + (0x10 * Index));

    UtStep (L"  port %u: portsc=0x%08x ccs=%u ped=%u pp=%u pls=%u speed=%u",
            Index, Portsc,
            Portsc & 0x1, (Portsc >> 1) & 0x1, (Portsc >> 9) & 0x1,
            (Portsc >> 5) & 0xF, (Portsc >> 10) & 0xF);
  }
}

/*
 * Ask the Qualcomm power-control protocol for bus power directly.
 *
 * UsbConfig's UsbEnableVbus only delegates here, and the vendor's own host
 * bring-up asks this protocol to turn VBUS OFF, never on. On this target the
 * power library reports "Invalid Port Index = 0" during boot, so the port
 * table it validates against is empty and every delegated call fails before
 * reaching the PMIC. Calling the protocol ourselves, across the plausible
 * port indices, says whether the lever exists at all - and the status codes
 * distinguish "no such protocol", "port rejected" and "PMIC refused".
 */
STATIC
VOID
UtProbeUsbPowerControl (VOID)
{
  EFI_GUID  PwrCtrlGuid = {
    0xe07df17e, 0xe79e, 0x4150,
    { 0x93, 0x78, 0x50, 0x62, 0x3a, 0x14, 0x99, 0x4a }
  };
  UT_USB_PWR_CTRL_PROTOCOL  *Pwr = NULL;
  EFI_STATUS                Status;
  UINT8                     Port;

  Status = gBS->LocateProtocol (&PwrCtrlGuid, NULL, (VOID **)&Pwr);
  if (EFI_ERROR (Status) || Pwr == NULL) {
    UtStep (L"usb power control: absent (%r)", Status);
    return;
  }
  UtStep (L"usb power control: rev=0x%Lx srcok=%c vbuson=%c",
          Pwr->Revision,
          (Pwr->GetVbusSrcOkStatus != NULL) ? L'Y' : L'-',
          (Pwr->SetVbusSourceEn != NULL) ? L'Y' : L'-');

  for (Port = 0; Port < 2; Port++) {
    BOOLEAN  State = FALSE;

    if (Pwr->GetVbusSrcOkStatus != NULL) {
      Status = Pwr->GetVbusSrcOkStatus (Port, &State);
      UtStep (L"  port %u srcok before: %r state=%u", Port, Status,
              (UINT32)State);
    }
    if (Pwr->SetVbusSourceEn != NULL) {
      Status = Pwr->SetVbusSourceEn (Port, TRUE);
      UtStep (L"  port %u SetVbusSourceEn(TRUE): %r", Port, Status);
    }
    if (Pwr->GetVbusSrcOkStatus != NULL) {
      State  = FALSE;
      Status = Pwr->GetVbusSrcOkStatus (Port, &State);
      UtStep (L"  port %u srcok after: %r state=%u", Port, Status,
              (UINT32)State);
    }
  }
}

/*
 * The charger protocol is the backend UsbPwrCtrlLib dispatches to, and it is
 * published on this device even though the port table in front of it is
 * empty. Calling it directly is the only path to VBUS that does not run
 * through the validation that answers Invalid Parameter for every index.
 *
 * Read-only throughout: every entry point here is a query. The write side
 * was removed once VBUS proved unreachable on this target.
 */

STATIC CONST CHAR16 *
UtSchgModeText (IN UINT32 Mode)
{
  switch (Mode) {
    case UT_SCHG_CONNECT_MODE_NONE: return L"none";
    case UT_SCHG_CONNECT_MODE_DFP:  return L"DFP";
    case UT_SCHG_CONNECT_MODE_UFP:  return L"UFP";
    default:                        return L"invalid";
  }
}

STATIC
VOID
UtProbePmicCharger (VOID)
{
  EFI_GUID  SchgGuid = {
    0xae6ae96e, 0x483f, 0x42ae,
    { 0x9c, 0xc1, 0x9f, 0xac, 0x1b, 0x58, 0x47, 0x28 }
  };
  UT_PMIC_SCHG_PROTOCOL      *Schg = NULL;
  UT_SCHG_TYPEC_PORT_STATUS   Port;
  EFI_STATUS                  Status;
  UINT8                       Active   = 0;
  UINT32                      Pmic     = 0;
  UINT32                      Value    = 0;
  BOOLEAN                     UsbinLive = TRUE;
  BOOLEAN                     Found;
  UINT32                      Preferred = UT_PMIC_NONE;
  UINTN                       Try;

  Status = gBS->LocateProtocol (&SchgGuid, NULL, (VOID **)&Schg);
  if (EFI_ERROR (Status) || Schg == NULL) {
    UtStep (L"pmic charger: absent (%r)", Status);
    return;
  }
  UtStep (L"pmic charger: rev=0x%Lx otg=%c role=%c",
          Schg->Revision,
          (Schg->EnableOtg != NULL) ? L'Y' : L'-',
          (Schg->SetTypeCPortRole != NULL) ? L'Y' : L'-');
  /* Index-free and read-only: these two separate "we asked with the wrong
   * device index" from "the charger backend is not answering at all", and
   * the first one names the valid indices outright. */
  if (Schg->SchgGetPmicInfo != NULL) {
    UT_SCHG_PMIC_INFO  Info;

    ZeroMem (&Info, sizeof (Info));
    Status = Schg->SchgGetPmicInfo (&Info);
    UtStep (L"  pmic info: %r count=%u idx=[%u %u %u %u]", Status,
            Info.ChargerCount, Info.PmicIndex[0], Info.PmicIndex[1],
            Info.PmicIndex[2], Info.PmicIndex[3]);
    if (!EFI_ERROR (Status) && Info.ChargerCount != 0 &&
        Info.PmicIndex[0] < 8) {
      Preferred = Info.PmicIndex[0];
    }
  }
  if (Schg->SchgGetChargerPmicIndex != NULL) {
    Status = Schg->SchgGetChargerPmicIndex (&Active);
    UtStep (L"  charger pmic index: %r value=%u", Status, (UINT32)Active);
    if (!EFI_ERROR (Status) && Active < 8) {
      Preferred = Active;
    }
  }


  /* PmicDeviceIndex is 0:Primary / 1:Secondary per the header. GetActivePort
   * is NOT that index - it returns an SDAM slot enum (SDAM_PORT_0..7), and
   * feeding its answer back in made every call return Device Error. Probe
   * the two real indices instead and keep whichever one the charger
   * actually answers on. */
  if (Schg->GetActivePort != NULL) {
    Status = Schg->GetActivePort (&Active);
    UtStep (L"  sdam active port: %r slot=%u (informational)", Status,
            (UINT32)Active);
  }

  /* Candidate order: whatever the charger named for itself, then the two
   * documented indices. UsbinValid is the probe because it is read-only and
   * every revision carries it. */
  Found = FALSE;
  for (Try = 0; Try < 3 && Schg->UsbinValid != NULL; Try++) {
    if (Try == 0) {
      if (Preferred == UT_PMIC_NONE) {
        continue;
      }
      Pmic = Preferred;
    } else {
      Pmic = (UINT32)(Try - 1);
      if (Pmic == Preferred) {
        continue;
      }
    }
    Status = Schg->UsbinValid (Pmic, &UsbinLive);
    UtStep (L"  pmic %u usbin valid: %r value=%u", Pmic, Status,
            (UINT32)UsbinLive);
    if (!EFI_ERROR (Status)) {
      Found = TRUE;
      break;
    }
  }
  if (!Found) {
    UsbinLive = TRUE;
    Pmic      = 0;
    UtStep (L"  no pmic index answered; charger backend is not responding");
  } else {
    UtStep (L"  pmic index=%u", Pmic);
  }

  if (Schg->GetConnectState != NULL) {
    Value  = UT_SCHG_CONNECT_MODE_INVALID;
    Status = Schg->GetConnectState (Pmic, &Value);
    UtStep (L"  connect state: %r mode=%s", Status, UtSchgModeText (Value));
  }
  if (Schg->GetPortState != NULL) {
    ZeroMem (&Port, sizeof (Port));
    Status = Schg->GetPortState (Pmic, &Port);
    UtStep (L"  port state: %r cc=%u ufp=%u vbus=%u dbnc=%u", Status,
            Port.CcOutSts, Port.UfpConnType, (UINT32)Port.VbusSts,
            (UINT32)Port.DebounceDoneSts);
  }
  if (Schg->GetOtgStatus != NULL) {
    Value  = UT_SCHG_OTG_STATUS_INVALID;
    Status = Schg->GetOtgStatus (Pmic, &Value);
    UtStep (L"  otg status: %r value=%u", Status, Value);
  }

  /*
   * No writes from here. Sourcing VBUS was tried and is not reachable on
   * this target: every SetVbusSourceEn is rejected by an empty port table,
   * and the charger that would fill it cannot start without DPP. What
   * remains is worth keeping as a read-only census of the power layer, so
   * the same ground can be re-measured on another target in one run without
   * touching a single control register.
   */
  if (UsbinLive) {
    UtStep (L"  usb input live; nothing on this screen writes anyway");
  }
}

/*
 * Offer every host-controller handle to the driver bindings. UsbBusDxe
 * binds EFI_USB2_HC_PROTOCOL, which XhciDxe installs partway through the
 * connect pass that started it - so the handle that exists at the end of
 * that pass has never been offered to the bus driver.
 */
STATIC
VOID
UtConnectHostControllers (VOID)
{
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count    = 0;
  UINTN       Index;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol,
                                          &gEfiUsb2HcProtocolGuid, NULL,
                                          &Count, &Handles)) ||
      Handles == NULL) {
    return;
  }
  for (Index = 0; Index < Count; Index++) {
    gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
  }
  FreePool (Handles);
}

STATIC
VOID
UtWaitForHost (VOID)
{
  UINTN  Step;

  for (Step = 0; Step < 16 && !UtHostIsUp (); Step++) {
    gBS->Stall (UT_ENUM_STEP_US);
  }
  UtStep (L"host settle: usb2hc=%u pciio=%u after %Lu ms",
          (UINT32)UtCountByProtocol (&gEfiUsb2HcProtocolGuid),
          (UINT32)UtCountByProtocol (&gEfiPciIoProtocolGuid),
          (UINT64)(Step * (UT_ENUM_STEP_US / 1000u)));
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
   * The vendor mode-switch protocol, measured on this device:
   * ToggleUsbMode stops the current mode and pins the static config to the
   * other one; the core then reads INVALID and the toggle refuses to run
   * from there. The completion trigger is the on-demand init event - the
   * UsbConfigDxe callback starts the core in the pinned mode. Neither half
   * works alone, and a bare StartController is overridden by the pin.
   */
  BeforeCount = UtConfigHandles (&Before);
  if (Cfg->ToggleUsbMode != NULL) {
    UtStep (L"state before toggle: mode=0x%x", Cfg->ModeType);
    Status = Cfg->ToggleUsbMode (Cfg, Capable);
    UtStep (L"ToggleUsbMode(core %u): %r", Capable, Status);
    if (!EFI_ERROR (Status)) {
      Toggled = TRUE;
      UtSignalUsbInit ();
      UtWaitForHost ();
      /*
       * Second trigger, evidence-backed: under the XHCI pin any vendor
       * start call lands in host mode - a restore attempt that asked for
       * DEVICE_SS returned Success with the state reading XHCI. If the
       * init event alone did not finish the job, ask directly.
       */
      if (!UtHostIsUp ()) {
        Status = Cfg->StartController (Cfg, Capable, QCOM_USB_DEVICE_MODE_SS);
        UtStep (L"pinned start (asks DEVICE, pin says XHCI): %r", Status);
        UtWaitForHost ();
      }
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

  /*
   * Bus power. A stick with no VBUS never enumerates, and the query itself
   * failing is a finding - the previous run printed no vbus row at all
   * because GetUsbVbusStatus errored and the raise was skipped with it. Ask
   * for power regardless of what the query says.
   */
  if (Cfg->GetUsbVbusStatus != NULL) {
    Vbus = 0xFFFFFFFF;
    Status = Cfg->GetUsbVbusStatus (Cfg, Capable, &Vbus);
    UtStep (L"vbus query: %r value=%u", Status, Vbus);
  } else {
    UtStep (L"vbus query: member absent");
  }
  if (Cfg->UsbEnableVbus != NULL) {
    Status = Cfg->UsbEnableVbus (Cfg, Capable);
    UtStep (L"UsbEnableVbus: %r", Status);
    if (Cfg->GetUsbVbusStatus != NULL) {
      Vbus = 0xFFFFFFFF;
      Status = Cfg->GetUsbVbusStatus (Cfg, Capable, &Vbus);
      UtStep (L"vbus after: %r value=%u", Status, Vbus);
    }
  }

  /*
   * UsbBusDxe binds the host controller handle, not the UsbConfig handle
   * the mode switch created - and Usb2Hc only appeared partway through that
   * earlier connect pass, so nothing has offered it to the bus driver yet.
   */
  UtProbeUsbPowerControl ();
  UtProbePmicCharger ();
  UtReportXhciPorts (Cfg, Capable);
  UtConnectHostControllers ();

  /*
   * Watch the bus, not just the filesystem: UsbIo is the first evidence a
   * device answered at all, and it separates "no device on the wire" from
   * "device present, storage stack did not finish".
   */
  SfsBefore = UtCountByProtocol (&gEfiSimpleFileSystemProtocolGuid);
  for (Step = 0; Step < UT_ENUM_STEPS; Step++) {
    gBS->Stall (UT_ENUM_STEP_US);
    if (UtCountByProtocol (&gEfiUsbIoProtocolGuid) != 0 &&
        UtCountByProtocol (&gEfiSimpleFileSystemProtocolGuid) > SfsBefore) {
      break;
    }
    if ((Step % 4) == 3) {
      UtConnectHostControllers ();
    }
  }
  UtStep (L"bus scan: usbio=%u blkio=%u sfs %Lu -> %Lu after %Lu ms",
          (UINT32)UtCountByProtocol (&gEfiUsbIoProtocolGuid),
          (UINT32)UtCountByProtocol (&gEfiBlockIoProtocolGuid),
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
   * Hand the core back to the gadget stack, through the same two-step
   * protocol the acquire used. The toggle-back only runs from a live host
   * mode (the vendor refuses from INVALID), so a core that never came up is
   * first started - per the pin, that lands it in XHCI - and then toggled.
   * "Device is back" means the USB function protocol did, not a mode field.
   */
  if (Toggled || Started) {
    UINTN  Pass;

    for (Pass = 0; Pass < 3; Pass++) {
      if (UtCountByProtocol (&gEfiUsbfnIoProtocolGuid) != 0) {
        break;
      }
      if (UtHostIsUp ()) {
        Restore = Cfg->ToggleUsbMode (Cfg, Capable);
        UtStep (L"restore toggle-back: %r", Restore);
      }
      UtSignalUsbInit ();
      for (Step = 0; Step < 8; Step++) {
        gBS->Stall (UT_ENUM_STEP_US);
        if (UtCountByProtocol (&gEfiUsbfnIoProtocolGuid) != 0) {
          break;
        }
      }
      UtStep (L"restore settle pass %u: usbfn=%u usb2hc=%u mode=0x%x",
              (UINT32)Pass,
              (UINT32)UtCountByProtocol (&gEfiUsbfnIoProtocolGuid),
              (UINT32)UtCountByProtocol (&gEfiUsb2HcProtocolGuid),
              Cfg->ModeType);
    }
    if (UtCountByProtocol (&gEfiUsbfnIoProtocolGuid) == 0) {
      UtStep (L"restore FAILED: no USB function after 3 passes");
    }
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
