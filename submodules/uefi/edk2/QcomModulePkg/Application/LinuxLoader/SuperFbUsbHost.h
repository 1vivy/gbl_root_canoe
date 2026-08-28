/*
 * USB host-role ownership for the boot menu.
 *
 * The peripheral gadget behind SuperFbMassStorage.c and the host stack this
 * file brings up are two mutually exclusive owners of the same USB core. The
 * gadget exports the phone's disks to a PC; the host stack reads a stick.
 * Whoever wants the core takes it explicitly and hands it back explicitly:
 * there is no arbitration below us.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SUPER_FB_USB_HOST_H__
#define __SUPER_FB_USB_HOST_H__

#include <Uefi.h>

/* The vendor protocol mirror lives in QcomModulePkg/Include/Protocol so the
 * standalone diagnostics tool (AndroidToolsPkg UsbTools) compiles against
 * the same definition this file drives. */
#include <Protocol/QcomUsbConfig.h>

/*
 * Report every USB-relevant protocol instance and its mode without changing
 * any controller state. Safe from any screen.
 *
 * This is the one census in the tree: what the resident firmware actually
 * dispatched is the only thing that decides whether a USB host stack is
 * reachable at all, and that answer differs by entry path (a fastboot-entered
 * session and a flashed-efisp normal boot do not present the same handle set),
 * so it has to be measurable from the menu rather than inferred.
 */
VOID
SfbUsbHostCensus (VOID);

/*
 * The same counts the census writes to the log, returned to the caller.
 *
 * The log is not a dependable channel for this answer: the platform defers
 * its flush until boot continues into an OS stage, so any run that faults -
 * exactly the runs worth diagnosing - loses every mark. The census screen
 * therefore shows these on the display, where a crash cannot take them.
 */
typedef struct {
  UINT32  Cfg;
  UINT32  Usb2Hc;
  UINT32  PciIo;
  UINT32  UsbIo;
  UINT32  BlockIo;
  UINT32  SimpleFs;
  UINT32  DriverBinding;
  /*
   * Capability, not population. The counts above say what got dispatched;
   * these say what the platform claims it could do. A target reporting host
   * capability on a core while Usb2Hc stays 0 is a missing XHCI stack, which
   * is a completely different problem from a core that cannot do host at all,
   * and the two are indistinguishable from handle counts.
   */
  UINT64  Revision;
  UINT32  CoreCount;
  /* GetSupUsbMode per core, in the client-selection vocabulary. Entries past
   * CoreCount, and cores the query failed for, are QCOM_USB_INVALID_MODE. */
  UINT32  CoreModes[QCOM_USB_CORE_MAX_NUM];
} SFB_USB_HOST_COUNTS;

VOID
SfbUsbHostCounts (OUT SFB_USB_HOST_COUNTS *Counts);

/*
 * Who owns the USB core, and in which role.
 *
 * This is an enum rather than an "owned" boolean because the distinction that
 * matters is not "are we in host mode" but "did *we* put it there". The
 * vendor's own UsbConfigDxe never stops a core it did not start - its disable
 * path skips any core whose mode table entry says it was never brought up -
 * and a loader that stops a core the vendor still owns, in a mode it is not
 * in, faults the machine. Two separate hardware crashes came from encoding
 * that as a boolean and getting it wrong.
 */
typedef enum {
  SfbUsbModeUnknown = 0,  /* not probed yet */
  SfbUsbModeVendor,       /* the vendor owns the core; we have not switched */
  SfbUsbModeHost,         /* we switched to XHCI host and it materialised */
  SfbUsbModeDevice        /* we switched back for a gadget consumer */
} SFB_USB_MODE;

/*
 * Ask for a role. The only entry point that changes controller state.
 *
 * SfbUsbModeHost: probe capability with GetSupUsbMode first and refuse
 * without touching anything when no core reports host support; otherwise
 * switch, build the stack down to Simple File System, and verify an
 * EFI_USB2_HC_PROTOCOL actually appeared. If none did, undo the switch and
 * return EFI_NOT_FOUND with the core disowned - a role change that produced
 * no host controller is a failure, whatever status the vendor call returned.
 *
 * SfbUsbModeDevice: a no-op returning EFI_SUCCESS unless we currently hold
 * SfbUsbModeHost. Handing back a core we never took is the crash above.
 *
 * Repeating the current mode re-runs only the connect and enumeration passes,
 * because media may have appeared since.
 */
EFI_STATUS
SfbUsbRequest (IN SFB_USB_MODE Want);

/* The role this loader believes it holds. Never probes; pure accessor. */
SFB_USB_MODE
SfbUsbCurrent (VOID);

/* Human-readable mode name for logs and the diagnostics screen. */
CONST CHAR8 *
SfbUsbModeText (IN SFB_USB_MODE Mode);

/*
 * Bind whatever has been plugged in since the last pass, when this loader
 * owns the core in host mode. No-op otherwise.
 *
 * A menu rebuild only re-walks the handles that already exist; nothing in it
 * runs ConnectController, so a stick inserted after boot is enumerated by
 * XHCI and then never bound up through UsbBus, mass storage, partition and
 * FAT. Removable media is the one thing a boot menu must expect to appear
 * late, so every rebuild re-runs the connect pass.
 *
 * Unlike the acquire this does not wait for arrival: by the time a rebuild
 * happens the operator has already inserted the medium, and charging every
 * redraw the acquire's multi-second settle would be paid on every menu.
 */
VOID
SfbUsbHostRescan (VOID);

#endif
