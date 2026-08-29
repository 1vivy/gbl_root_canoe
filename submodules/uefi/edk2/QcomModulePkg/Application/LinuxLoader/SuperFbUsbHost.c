/*
 * USB host-role ownership for the boot menu.
 *
 * Reading a stick needs the host chain UsbConfig (host role) -> XHCI PCI
 * emulation -> XHCI -> UsbBus -> UsbMassStorage -> BlockIo. Every link above
 * the first is already resident and binds itself once the controller handle
 * appears; the one action nothing in this loader's boot path performs is the
 * role switch that creates that handle.
 *
 * A blind gBS->ConnectController pass cannot do it. In the device-mode world a
 * fastboot boot leaves behind, only the peripheral handle exists, so there is
 * nothing for the XHCI PCI-emulation binding to match on. The platform's own
 * StartController is what installs the host handle and connects it.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbUsbHost.h"
#include "SuperFbMenu.h"

#include <FastbootLib/FastbootMain.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
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

/* Keeps the translation unit legal when the feature is compiled out. */
CONST CHAR8 *gSfbUsbHostModuleTag = "SuperFbUsbHost";

/* TRUE only between a successful acquire and its release. */
STATIC BOOLEAN mSfbUsbHostOwned = FALSE;

/* The role this loader believes it holds. Distinct from mSfbUsbHostOwned,
 * which records only whether the acquire succeeded; this also remembers that
 * we handed the core back, so a second release cannot poke the vendor stack. */
STATIC SFB_USB_MODE mSfbUsbMode = SfbUsbModeUnknown;

/*
 * TRUE when a connect pass has just run and nothing can have arrived since.
 *
 * The acquire ends with one, and the first menu build follows it immediately;
 * re-running ConnectController over every controller that soon, with the core
 * freshly switched to host mode, buys nothing and is not free. The rescan
 * spends this flag once and then does real work on every later rebuild, which
 * is where a late-inserted stick actually shows up.
 */
STATIC BOOLEAN mSfbUsbHostConnectFresh = FALSE;

/* The core the acquire actually won. Release must name the same one, and the
 * ladder below may settle on core 1. */
STATIC UINT32 mSfbUsbHostCore = QCOM_USB_CORE_0;

/* Wall time the enumeration poll is allowed to spend waiting for a stick. */
#define SFB_USB_HOST_ENUM_STEP_US    (250u * 1000u)
#define SFB_USB_HOST_ENUM_STEPS      12u

STATIC
UINTN
SfbUsbHostCountByProtocol (IN CONST EFI_GUID *Protocol)
{
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count = 0;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, (EFI_GUID *)Protocol,
                                          NULL, &Count, &Handles)) ||
      Handles == NULL) {
    return 0;
  }
  FreePool (Handles);
  return Count;
}

/* Defined below; the census needs it before the file gets there. */
STATIC QCOM_USB_CONFIG_PROTOCOL *SfbUsbHostConfig (VOID);

VOID
SfbUsbHostCounts (OUT SFB_USB_HOST_COUNTS *Counts)
{
  QCOM_USB_CONFIG_PROTOCOL  *Cfg;
  UINT32                    Index;

  if (Counts == NULL) {
    return;
  }
  Counts->Cfg     = (UINT32)SfbUsbHostCountByProtocol (&gQcomUsbConfigProtocolGuid);
  Counts->Usb2Hc  = (UINT32)SfbUsbHostCountByProtocol (&gEfiUsb2HcProtocolGuid);
  Counts->PciIo   = (UINT32)SfbUsbHostCountByProtocol (&gEfiPciIoProtocolGuid);
  Counts->UsbIo   = (UINT32)SfbUsbHostCountByProtocol (&gEfiUsbIoProtocolGuid);
  Counts->BlockIo = (UINT32)SfbUsbHostCountByProtocol (&gEfiBlockIoProtocolGuid);
  Counts->SimpleFs =
    (UINT32)SfbUsbHostCountByProtocol (&gEfiSimpleFileSystemProtocolGuid);
  /*
   * gEfiDriverBindingProtocolGuid is counted as a proxy for "the resident
   * host drivers were dispatched at all": a firmware volume that never
   * carried XhciDxe and friends publishes far fewer bindings, and that is a
   * different failure from a role switch that did not take.
   */
  Counts->DriverBinding =
    (UINT32)SfbUsbHostCountByProtocol (&gEfiDriverBindingProtocolGuid);

  Counts->Revision  = 0;
  Counts->CoreCount = 0;
  for (Index = 0; Index < QCOM_USB_CORE_MAX_NUM; Index++) {
    Counts->CoreModes[Index] = QCOM_USB_INVALID_MODE;
  }

  /*
   * Capability probe. Read-only by construction: GetCoreCount and
   * GetSupUsbMode are queries, so this stays safe to run from any screen,
   * which is the whole point of the census.
   */
  Cfg = SfbUsbHostConfig ();
  if (Cfg == NULL) {
    return;
  }
  Counts->Revision = Cfg->Revision;

  if (Cfg->GetCoreCount != NULL) {
    UINT8  Reported = 0;

    if (!EFI_ERROR (Cfg->GetCoreCount (Cfg, &Reported)) &&
        Reported <= QCOM_USB_CORE_MAX_NUM) {
      Counts->CoreCount = Reported;
    }
  }

  if (Cfg->GetSupUsbMode != NULL) {
    UINT32  Limit = (Counts->CoreCount != 0) ? Counts->CoreCount
                                             : QCOM_USB_CORE_MAX_NUM;

    for (Index = 0; Index < Limit; Index++) {
      UINT32  Modes = 0;

      if (!EFI_ERROR (Cfg->GetSupUsbMode (Cfg, Index, &Modes))) {
        Counts->CoreModes[Index] = Modes;
      }
    }
  }
}

VOID
SfbUsbHostCensus (VOID)
{
  EFI_HANDLE           *Handles = NULL;
  UINTN                 CfgCount = 0;
  UINTN                 Index;
  SFB_USB_HOST_COUNTS   Counts;

  SfbUsbHostCounts (&Counts);
  DEBUG ((EFI_D_ERROR,
          "SFB: MARK usbhost-census cfg=%u usb2hc=%u pciio=%u usbio=%u "
          "blkio=%u sfs=%u db=%u\n",
          Counts.Cfg, Counts.Usb2Hc, Counts.PciIo, Counts.UsbIo,
          Counts.BlockIo, Counts.SimpleFs, Counts.DriverBinding));

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol,
                                          (EFI_GUID *)&gQcomUsbConfigProtocolGuid,
                                          NULL, &CfgCount, &Handles)) ||
      Handles == NULL) {
    return;
  }

  for (Index = 0; Index < CfgCount; Index++) {
    QCOM_USB_CONFIG_PROTOCOL  *Cfg = NULL;

    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index],
                                        (EFI_GUID *)&gQcomUsbConfigProtocolGuid,
                                        (VOID **)&Cfg)) ||
        Cfg == NULL) {
      continue;
    }
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK usbhost-core idx=%u rev=0x%Lx core=%u mode=0x%x "
            "always=%u\n",
            (UINT32)Index, Cfg->Revision, Cfg->CoreNum, Cfg->ModeType,
            (UINT32)Cfg->AlwaysConnected));
  }

  FreePool (Handles);
}

/* Locate the sentinel UsbConfig instance, or NULL when the resident firmware
 * carries no UsbConfig at all. */
STATIC
QCOM_USB_CONFIG_PROTOCOL *
SfbUsbHostConfig (VOID)
{
  QCOM_USB_CONFIG_PROTOCOL  *Cfg = NULL;

  if (EFI_ERROR (gBS->LocateProtocol ((EFI_GUID *)&gQcomUsbConfigProtocolGuid,
                                      NULL, (VOID **)&Cfg))) {
    return NULL;
  }
  return Cfg;
}

/*
 * The UsbConfig instance bound to Core, or NULL.
 *
 * The vendor installs one protocol instance per core, and its start path
 * reads the core number from the instance, not from the CoreNum argument:
 * XhcPciEmulation.c:640-675 calls ConfigUsb / GetUsbHostConfig /
 * GetCoreBaseAddr with pUsbConfigProtocol->coreNum throughout. LocateProtocol
 * hands back the first registered instance, and on this device the census
 * shows that is the core-1 instance (usbhost-core idx=0 core=1). Starting
 * core 0 through it asks the vendor for a core whose MMIO base does not
 * exist, and the shim's first capability register read on that base faults -
 * the crash observed at the host:start mark.
 */
STATIC
QCOM_USB_CONFIG_PROTOCOL *
SfbUsbHostConfigForCore (IN UINT32 Core)
{
  EFI_HANDLE               *Handles = NULL;
  UINTN                    Count    = 0;
  UINTN                    Index;
  QCOM_USB_CONFIG_PROTOCOL  *Found   = NULL;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol,
                                          (EFI_GUID *)&gQcomUsbConfigProtocolGuid,
                                          NULL, &Count, &Handles)) ||
      Handles == NULL) {
    return NULL;
  }

  for (Index = 0; Index < Count && Found == NULL; Index++) {
    QCOM_USB_CONFIG_PROTOCOL  *Cfg = NULL;

    if (!EFI_ERROR (gBS->HandleProtocol (Handles[Index],
                                         (EFI_GUID *)&gQcomUsbConfigProtocolGuid,
                                         (VOID **)&Cfg)) &&
        Cfg != NULL && Cfg->CoreNum == Core) {
      Found = Cfg;
    }
  }

  FreePool (Handles);
  return Found;
}

/* TRUE when any UsbConfig instance already reports XHCI host mode; the core it
 * reports is written back so a release names the right one. */
STATIC
BOOLEAN
SfbUsbHostAlreadyHost (OUT UINT32 *Core)
{
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count = 0;
  UINTN       Index;
  BOOLEAN     Found = FALSE;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol,
                                          (EFI_GUID *)&gQcomUsbConfigProtocolGuid,
                                          NULL, &Count, &Handles)) ||
      Handles == NULL) {
    return FALSE;
  }

  for (Index = 0; Index < Count && !Found; Index++) {
    QCOM_USB_CONFIG_PROTOCOL  *Cfg = NULL;

    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index],
                                        (EFI_GUID *)&gQcomUsbConfigProtocolGuid,
                                        (VOID **)&Cfg)) ||
        Cfg == NULL) {
      continue;
    }
    if (Cfg->ModeType == QCOM_USB_HOST_MODE_XHCI) {
      *Core = Cfg->CoreNum;
      Found = TRUE;
    }
  }

  FreePool (Handles);
  return Found;
}

/* The current UsbConfig handle set, in pool memory. */
STATIC
UINTN
SfbUsbHostConfigHandles (OUT EFI_HANDLE **Handles)
{
  UINTN  Count = 0;

  *Handles = NULL;
  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol,
                                          (EFI_GUID *)&gQcomUsbConfigProtocolGuid,
                                          NULL, &Count, Handles))) {
    *Handles = NULL;
    return 0;
  }
  return Count;
}

/*
 * Offer only the handles a start created to the driver bindings.
 *
 * The vendor's StartController connects the host handle itself on the
 * reference flow, but on this build the shim was left unbound (pci=0), so
 * the offer is repeated here - to the delta only, which by construction
 * excludes every pre-existing handle. A pass over every UsbConfig handle is
 * never acceptable: the stale peripheral handle keeps the same UsbConfig
 * struct after the flip, reads modeType==XHCI, and the shim's Supported()
 * accepts it, double-binding one controller to two shim instances.
 */
STATIC
VOID
SfbUsbHostBindNewHandles (IN EFI_HANDLE *Before, IN UINTN BeforeCount)
{
  EFI_HANDLE  *After = NULL;
  UINTN       AfterCount;
  UINTN       Outer;
  UINTN       Inner;
  BOOLEAN     Known;

  AfterCount = SfbUsbHostConfigHandles (&After);
  for (Outer = 0; Outer < AfterCount; Outer++) {
    Known = FALSE;
    for (Inner = 0; Inner < BeforeCount; Inner++) {
      if (After[Outer] == Before[Inner]) {
        Known = TRUE;
        break;
      }
    }
    if (!Known) {
      gBS->ConnectController (After[Outer], NULL, NULL, TRUE);
    }
  }
  if (After != NULL) {
    FreePool (After);
  }
}

/*
 * Ask the platform for host mode, escalating through a fixed ladder and
 * stopping at the first success. Every rung is logged, because which one won
 * is the whole diagnostic value of this function.
 *
 * StopController is deliberately never called first: the platform's own
 * StartController stops whatever mode the core is in before it starts the
 * requested one, and a redundant stop naming a mode the core is not in is
 * rejected outright.
 */
/*
 * Load the USB host driver chain from the boot root, in dependency order.
 *
 * The resident firmware has no host stack: `abl_a`, the image this loader
 * replaces, contains exactly one module and no DXEs, and the USB drivers that
 * do exist here come from XBL, which carries only the device side. The four
 * below live in the device's own `uefi_a` volume, which is not on the Android
 * boot path, so whoever wants a host stack has to load it.
 *
 * Order is load-bearing: each binds on what the previous produces
 * (PciIo -> Usb2Hc -> UsbIo -> BlockIo). Depex is not evaluated on this path -
 * only the DXE dispatcher does that - so the sequencing is ours to get right.
 *
 * Loading only registers driver bindings; nothing attaches until the core is
 * actually in XHCI mode. That is why this runs before StartController rather
 * than after: XhcPciEmulationDriverBindingSupported rejects a UsbConfig handle
 * whose modeType is not already USB_HOST_MODE_XHCI, so the bindings have to be
 * in place when StartController flips the mode and runs its connect pass.
 */
STATIC BOOLEAN mSfbUsbStackTried = FALSE;

STATIC
VOID
SfbUsbHostLoadStack (VOID)
{
  STATIC CONST CHAR16 *CONST Stack[] = {
    L"XhciPciEmulation.efi",
    L"XhciDxe.efi",
    L"UsbBusDxe.efi",
    L"UsbMassStorageDxe.efi"
  };
  /*
   * SFB_CONFIG is 7056 bytes and this runs deep inside the menu loop, whose
   * own SFB_MENU_STATE already carries 32 entries by value. Pool, not stack.
   */
  SFB_CONFIG  *Config = NULL;
  EFI_HANDLE  Volume = NULL;
  UINTN       Index;
  UINTN       Loaded = 0;

  /* Once per boot: a second pass would re-register bindings already present. */
  if (mSfbUsbStackTried) {
    return;
  }
  mSfbUsbStackTried = TRUE;

  Config = AllocateZeroPool (sizeof (*Config));
  if (Config == NULL) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK usbhost-stack status=no-memory\n"));
    return;
  }

  if (EFI_ERROR (SfbLoadBootConfig (Config, &Volume)) || Volume == NULL) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK usbhost-stack status=no-boot-root\n"));
    FreePool (Config);
    return;
  }
  FreePool (Config);

  for (Index = 0; Index < ARRAY_SIZE (Stack); Index++) {
    CHAR16      Path[SFB_PATH_CHARS];
    EFI_STATUS  Status;

    UnicodeSPrint (Path, sizeof (Path), L"%s\\usbhost\\%s",
                   SfbVolumeRootPrefix (Volume), Stack[Index]);
    Status = SfbLoadDriver (Volume, Path);
    DEBUG ((EFI_D_ERROR, "SFB: MARK usbhost-driver name=%s status=%r\n",
            Stack[Index], Status));
    if (!EFI_ERROR (Status)) {
      Loaded++;
    }
  }

  DEBUG ((EFI_D_ERROR, "SFB: MARK usbhost-stack loaded=%u of=%u\n",
          (UINT32)Loaded, (UINT32)ARRAY_SIZE (Stack)));
}

/*
 * TRUE when any core reports host or dual-role capability.
 *
 * Pure query: GetSupUsbMode changes nothing. This gate exists because every
 * rung of the ladder below is a write to a core the vendor owns, and on a
 * target with no host-capable core every one of those writes is both useless
 * and dangerous - the vendor's own disable path refuses to stop a core it did
 * not start, and this loader faulted the machine twice by doing exactly that.
 *
 * Answers in the client-selection vocabulary, so the test is against
 * QCOM_USB_HOST_MODE / QCOM_USB_DUAL_ROLE_MODE, never QCOM_USB_HOST_MODE_XHCI.
 */
STATIC
BOOLEAN
SfbUsbHostAnyCapableCore (IN QCOM_USB_CONFIG_PROTOCOL *Cfg, OUT UINT32 *First)
{
  UINT32  Limit = QCOM_USB_CORE_MAX_NUM;
  UINT8   Reported = 0;
  UINT32  Index;

  *First = QCOM_USB_CORE_0;

  if (Cfg->GetSupUsbMode == NULL) {
    /* No way to ask. Refuse rather than probe blindly: an unanswerable
     * question is not permission to start writing to controllers. */
    DEBUG ((EFI_D_ERROR, "SFB: MARK usbhost-cap absent=getsupusbmode\n"));
    return FALSE;
  }
  if (Cfg->GetCoreCount != NULL &&
      !EFI_ERROR (Cfg->GetCoreCount (Cfg, &Reported)) &&
      Reported > 0 && Reported <= QCOM_USB_CORE_MAX_NUM) {
    Limit = Reported;
  }

  for (Index = 0; Index < Limit; Index++) {
    UINT32      Modes = 0;
    EFI_STATUS  Query = Cfg->GetSupUsbMode (Cfg, Index, &Modes);

    DEBUG ((EFI_D_ERROR, "SFB: MARK usbhost-cap core=%u status=%r modes=0x%x\n",
            Index, Query, Modes));
    if (!EFI_ERROR (Query) &&
        (Modes & (QCOM_USB_HOST_MODE | QCOM_USB_DUAL_ROLE_MODE)) != 0) {
      *First = Index;
      return TRUE;
    }
  }
  return FALSE;
}

STATIC
EFI_STATUS
SfbUsbHostStart (IN QCOM_USB_CONFIG_PROTOCOL *Cfg, OUT UINT32 *Core)
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Before      = NULL;
  UINTN       BeforeCount = 0;
  UINT32      Capable     = QCOM_USB_CORE_0;

  *Core = QCOM_USB_CORE_0;

  /*
   * Nothing below this point is read-only, so the capability gate comes
   * first. Measured on the OnePlus 15 under fastboot: UsbConfig is resident
   * three times over, StartController returns success, and no
   * EFI_USB2_HC_PROTOCOL ever appears - the writes accomplish nothing and the
   * restore that follows them faults the machine.
   */
  if (!SfbUsbHostAnyCapableCore (Cfg, &Capable)) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK usbhost-start status=%r try=0 reason=nocap\n",
            EFI_UNSUPPORTED));
    return EFI_UNSUPPORTED;
  }

  /*
   * Drive the core through its own instance. The vendor reads the core
   * number from the protocol instance, so the query instance LocateProtocol
   * returned - any core's - cannot start a core it is not bound to.
   */
  Cfg = SfbUsbHostConfigForCore (Capable);
  if (Cfg == NULL) {
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK usbhost-start status=%r try=0 reason=noinst core=%u\n",
            EFI_NOT_FOUND, Capable));
    return EFI_NOT_FOUND;
  }

  /*
   * Register the host driver bindings before the mode flips. StartController
   * creates the host handle and runs its own connect pass, and the shim's
   * Supported() only accepts a UsbConfig handle already in XHCI mode - so a
   * binding registered after that call has missed its window.
   */
  SfbUsbHostLoadStack ();

  BeforeCount = SfbUsbHostConfigHandles (&Before);

  *Core = Capable;
  Status = Cfg->StartController (Cfg, Capable, QCOM_USB_HOST_MODE_XHCI);
  DEBUG ((EFI_D_ERROR, "SFB: MARK usbhost-start status=%r core=%u try=1\n",
          Status, Capable));
  if (!EFI_ERROR (Status)) {
    SfbUsbHostBindNewHandles (Before, BeforeCount);
    if (Before != NULL) {
      FreePool (Before);
    }
    return Status;
  }

  /*
   * A core that has never been started at all may need the vendor's own
   * bring-up first: a flashed-efisp boot censuses no USB function protocol,
   * which proves the controller stays down until that event group is
   * signalled, and the platform reaches StartController from the same
   * callback. The cost when it was not needed is one device-mode blip.
   */
  SfbUsbControllerInit ();
  Status = Cfg->StartController (Cfg, Capable, QCOM_USB_HOST_MODE_XHCI);
  DEBUG ((EFI_D_ERROR, "SFB: MARK usbhost-start status=%r core=%u try=2\n",
          Status, Capable));
  if (!EFI_ERROR (Status)) {
    SfbUsbHostBindNewHandles (Before, BeforeCount);
  }
  if (Before != NULL) {
    FreePool (Before);
  }

  /*
   * There is deliberately no ConfigUsb fallback. It looks like the decomposed
   * form of StartController, but the vendor source is explicit that it is not:
   * XhcPciEmulationDriverBindingSupported gates on
   * `pUsbConfigProtocol->modeType == USB_HOST_MODE_XHCI`, and ConfigUsb does
   * not update modeType or create the host handle. Connecting after it can
   * never bind the shim, so the rung was pure risk - a write to a vendor-owned
   * core that cannot possibly succeed.
   *
   * No further cores are tried either: SfbUsbHostAnyCapableCore already
   * scanned every core the protocol admits to.
   */
  return Status;
}

/*
 * A stick with no bus power never enumerates. The Type-C role cannot be set
 * through the PMIC on this family, so the config protocol's own VBUS lever is
 * the only one there is, and it may still not raise it - a self-powered hub is
 * the fallback, and that is an operator action rather than a code path.
 */
STATIC
VOID
SfbUsbHostEnableVbus (IN QCOM_USB_CONFIG_PROTOCOL *Cfg, IN UINT32 Core)
{
  EFI_STATUS  Status;
  UINT32      Vbus = QCOM_USB_VBUS_DISABLED;

  if (Cfg->GetUsbVbusStatus == NULL || Cfg->UsbEnableVbus == NULL) {
    return;
  }
  if (EFI_ERROR (Cfg->GetUsbVbusStatus (Cfg, Core, &Vbus)) ||
      Vbus != QCOM_USB_VBUS_DISABLED) {
    return;
  }
  Status = Cfg->UsbEnableVbus (Cfg, Core);
  DEBUG ((EFI_D_ERROR, "SFB: MARK usbhost-vbus was=%u status=%r\n",
          (UINT32)Vbus, Status));
}

/*
 * Wait for a stick to finish enumerating. USB enumeration plus the SCSI
 * capacity exchange behind BlockIo is not instantaneous and there is no
 * arrival event to wait on, so this polls. Zero growth is not an error: the
 * operator may have attached nothing.
 */
STATIC
VOID
SfbUsbHostAwaitVolumes (VOID)
{
  UINTN  Before;
  UINTN  After;
  UINTN  Step;

  Before = SfbUsbHostCountByProtocol (&gEfiSimpleFileSystemProtocolGuid);
  After = Before;

  for (Step = 0; Step < SFB_USB_HOST_ENUM_STEPS && After <= Before; Step++) {
    gBS->Stall (SFB_USB_HOST_ENUM_STEP_US);
    SfbConnectAll ();
    After = SfbUsbHostCountByProtocol (&gEfiSimpleFileSystemProtocolGuid);
  }

  DEBUG ((EFI_D_ERROR,
          "SFB: MARK usbhost-enum sfs-before=%u sfs-after=%u waited=%ums\n",
          (UINT32)Before, (UINT32)After,
          (UINT32)(Step * (SFB_USB_HOST_ENUM_STEP_US / 1000u))));
}

STATIC
EFI_STATUS
SfbUsbHostAcquire (VOID)
{
  EFI_STATUS               Status;
  QCOM_USB_CONFIG_PROTOCOL  *Cfg;
  UINT32                   Core = QCOM_USB_CORE_0;
  BOOLEAN                  Switched = FALSE;
  UINTN                    Controllers;

  Cfg = SfbUsbHostConfig ();
  if (Cfg == NULL) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK usbhost-start status=%r try=0\n",
            EFI_UNSUPPORTED));
    return EFI_UNSUPPORTED;
  }

  if (SfbUsbHostAlreadyHost (&Core)) {
    /* Already host-owned, by us on a previous call or by the platform. Only
     * the connect and enumeration passes are re-run: media may have appeared
     * since, and re-starting a running core would tear down what is there. */
    mSfbUsbHostCore = Core;
  } else {
    Status = SfbUsbHostStart (Cfg, &Core);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    mSfbUsbHostCore = Core;
    Switched = TRUE;
  }

  /*
   * Every later vtable call for this core goes through the instance bound to
   * it: the vendor reads the core number from the instance, so the query
   * instance from LocateProtocol would drive the wrong controller here too.
   */
  Cfg = SfbUsbHostConfigForCore (Core);
  if (Cfg == NULL) {
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK usbhost-start status=%r reason=noinst core=%u\n",
            EFI_NOT_FOUND, Core));
    mSfbUsbHostOwned = FALSE;
    return EFI_NOT_FOUND;
  }
  if (Switched) {
    SfbUsbHostEnableVbus (Cfg, Core);
  }

  /* SfbStartFatStack's connect pass is what binds the new UsbIo children down
   * through mass storage, BlockIo and DiskIo to the linked FAT driver. */
  SfbStartFatStack ();
  /* The handle set just changed and the volume classification cache is keyed
   * on handles, so a stale entry would misclassify a recycled handle. */
  SfbResetVolumeClassCache ();

  SfbUsbHostAwaitVolumes ();

  Controllers = SfbUsbHostCountByProtocol (&gEfiUsb2HcProtocolGuid);

  /*
   * Ownership means a host stack actually exists, not that StartController
   * returned success. On a firmware whose host DXEs never bind, the call
   * succeeds and no EFI_USB2_HC_PROTOCOL ever appears; claiming the core
   * anyway leaves it in host mode with no stack, and every later release
   * would then restart device mode for a host stack that never was. That is
   * a poke at the one component this loader cannot replace, on the two
   * paths - the export and the launch handoff - where it does the most
   * damage.
   *
   * So the acquire is transactional: if we changed the mode and got nothing
   * for it, change it back here, at the one point where the state is known,
   * and report failure with the core disowned.
   */
  if (Controllers == 0) {
    EFI_STATUS  Start = EFI_SUCCESS;

    if (Switched) {
      /*
       * The vendor's own mode-switch flows never call StopController from
       * the outside: UsbStartController stops the current mode itself, in
       * the order its state machine expects. A naked StopController here
       * faulted the device at the host:stop mark.
       */
      Start = Cfg->StartController (Cfg, mSfbUsbHostCore,
                                    QCOM_USB_DEVICE_MODE_SS);
      SfbResetVolumeClassCache ();
    }

    mSfbUsbHostOwned = FALSE;
    mSfbUsbHostConnectFresh = FALSE;
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK usbhost-abandon switched=%u restore=%r\n",
            (UINT32)Switched, Start));
    return EFI_NOT_FOUND;
  }

  mSfbUsbHostOwned = TRUE;
  mSfbUsbHostConnectFresh = TRUE;
  return EFI_SUCCESS;
}

VOID
SfbUsbHostRescan (VOID)
{
  if (!mSfbUsbHostOwned) {
    return;
  }

  /*
   * The acquire that just ran ended with a connect pass and an arrival wait.
   * The first menu build lands here immediately afterwards, and repeating
   * ConnectController across every controller that soon - on a core switched
   * to host mode moments ago - is work with nothing to find.
   */
  if (mSfbUsbHostConnectFresh) {
    mSfbUsbHostConnectFresh = FALSE;
    return;
  }

  /* The connect pass inside SfbStartFatStack is what binds a newly arrived
   * device up through UsbBus, mass storage, partition and FAT. */
  SfbStartFatStack ();
  /* The classification cache is keyed on handles and a rebind recycles
   * them, so a stale entry would misclassify the volume it lands on. */
  SfbResetVolumeClassCache ();
}

/* Disconnect every controller that publishes Protocol. */
STATIC
VOID
SfbDisconnectByProtocol (IN EFI_GUID *Protocol)
{
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count = 0;
  UINTN       Index;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, Protocol, NULL,
                                          &Count, &Handles)) ||
      Handles == NULL) {
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    gBS->DisconnectController (Handles[Index], NULL, NULL);
  }

  FreePool (Handles);
}

STATIC
VOID
SfbUsbHostRelease (VOID)
{
  QCOM_USB_CONFIG_PROTOCOL  *Cfg;
  EFI_STATUS               Start;

  /*
   * Never touch the core we did not take. Release runs on paths that are
   * reached whether or not host mode was ever acquired - the export, the
   * fastboot row, every launch - and a StopController on a core the vendor
   * stack still owns in device mode is a gratuitous poke at the one component
   * this loader cannot replace.
   */
  if (!mSfbUsbHostOwned) {
    return;
  }

  Cfg = SfbUsbHostConfigForCore (mSfbUsbHostCore);
  if (Cfg == NULL) {
    mSfbUsbHostOwned = FALSE;
    return;
  }

  /*
   * Undo what the acquire brought up on the USB side. Disconnecting each USB2
   * host controller stops the bus, mass-storage, partition and FAT stack
   * built on it and halts XHCI; on this platform the XHCI PCI-emulation shim
   * installs its emulated PCI I/O onto the very handle it binds, so that one
   * pass also runs the shim's Stop ().
   *
   * Scoped to Usb2Hc on purpose. A second sweep over every gEfiPciIoProtocol
   * handle would also tear down controllers this loader never started,
   * including the internal storage the caller is about to read, and freeing a
   * partition handle out from under an in-flight export is a use-after-free
   * rather than a tidy-up.
   */
  SfbDisconnectByProtocol (&gEfiUsb2HcProtocolGuid);

  /*
   * No StopController from here: UsbStartController stops the current mode
   * itself, in the order the vendor state machine expects. The vendor's own
   * mode-switch flows are built that way, and a naked StopController on this
   * device faulted the machine from the abandon path.
   */
  Start = Cfg->StartController (Cfg, mSfbUsbHostCore, QCOM_USB_DEVICE_MODE_SS);

  SfbResetVolumeClassCache ();
  mSfbUsbHostOwned = FALSE;
  mSfbUsbHostConnectFresh = FALSE;

  DEBUG ((EFI_D_ERROR, "SFB: MARK usbhost-release restore=%r\n", Start));
}

CONST CHAR8 *
SfbUsbModeText (IN SFB_USB_MODE Mode)
{
  switch (Mode) {
    case SfbUsbModeVendor: return "vendor";
    case SfbUsbModeHost:   return "host";
    case SfbUsbModeDevice: return "device";
    default:               return "unknown";
  }
}

SFB_USB_MODE
SfbUsbCurrent (VOID)
{
  return mSfbUsbMode;
}

EFI_STATUS
SfbUsbRequest (IN SFB_USB_MODE Want)
{
  SFB_USB_MODE  Was = mSfbUsbMode;
  EFI_STATUS    Status;

  switch (Want) {
    case SfbUsbModeHost:
      Status = SfbUsbHostAcquire ();
      /* The acquire is transactional: it only reports success once an
       * EFI_USB2_HC_PROTOCOL exists, and undoes its own switch otherwise. */
      mSfbUsbMode = EFI_ERROR (Status) ? SfbUsbModeVendor : SfbUsbModeHost;
      break;

    case SfbUsbModeDevice:
      /*
       * Only hand back a core we actually took. In every other state this is
       * deliberately a no-op: the vendor still owns the core, its gadget may
       * be live on it, and stopping a mode it is not in is what faulted the
       * device from both the export and the launch handoff.
       */
      if (mSfbUsbMode != SfbUsbModeHost) {
        DEBUG ((EFI_D_INFO, "SFB: MARK usb-mode from=%a to=device skipped\n",
                SfbUsbModeText (mSfbUsbMode)));
        return EFI_SUCCESS;
      }
      SfbUsbHostRelease ();
      mSfbUsbMode = SfbUsbModeDevice;
      Status = EFI_SUCCESS;
      break;

    default:
      return EFI_INVALID_PARAMETER;
  }

  DEBUG ((EFI_D_ERROR,
          "SFB: MARK usb-mode from=%a to=%a status=%r usb2hc=%u\n",
          SfbUsbModeText (Was), SfbUsbModeText (mSfbUsbMode), Status,
          (UINT32)SfbUsbHostCountByProtocol (&gEfiUsb2HcProtocolGuid)));
  return Status;
}
