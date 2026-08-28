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

/*
 * QCOM_USB_CONFIG_PROTOCOL, mirrored locally rather than pulled in from a
 * vendor include path, exactly as SuperFbMassStorage.h mirrors the MSD
 * protocol. GUID e722b03f-b250-42ce-8ebd-5bd51812d037.
 *
 * Members this loader calls are typed; the rest are VOID * so the vtable
 * offsets stay exact. Member order is load-bearing - the struct is only ever
 * read through a pointer the platform handed us, so a reordered field is a
 * call to the wrong function, not a compile error.
 *
 * The two enum-typed fields are UINT32 and AlwaysConnected is UINT8; the
 * vendor declares them as two enums plus UINT8, and both enums fit in int, so
 * the widths agree on AArch64 LP64.
 */
#define SFB_USB_CORE_0            0u
#define SFB_USB_CORE_1            1u
#define SFB_USB_CORE_2            2u
#define SFB_USB_CORE_MAX_NUM      6u

/*
 * QCOM_USB_MODE_TYPE carries two disjoint value sets in one enum, and mixing
 * them is a silent wrong answer rather than a type error.
 *
 * Controller-interface values are what StartController/StopController/ConfigUsb
 * take. Client-selection values are what GetSupUsbMode reports. A capability
 * test against GetSupUsbMode must use SFB_USB_HOST_MODE, never
 * SFB_USB_HOST_MODE_XHCI.
 */
#define SFB_USB_HOST_MODE_XHCI    0x00000001u  /* interface: pass to Start */
#define SFB_USB_DEVICE_MODE_SS    0x00000004u  /* interface: pass to Start */
#define SFB_USB_HOST_MODE         0x00000008u  /* capability: from GetSupUsbMode */
#define SFB_USB_DEVICE_MODE       0x00000010u  /* capability: from GetSupUsbMode */
#define SFB_USB_DUAL_ROLE_MODE    0x00000020u  /* capability: from GetSupUsbMode */
#define SFB_USB_INVALID_MODE      0x00010000u
#define SFB_VBUS_STATUS_DISABLED  0u

/* Vendor protocol revisions. A member added in revision N is only present
 * when Revision >= that value; reading further is a read past the end. */
#define SFB_USB_CFG_REVISION_1    0x0000000000010006ULL
#define SFB_USB_CFG_REVISION_2    0x0000000000020001ULL
#define SFB_USB_CFG_REVISION_3    0x0000000000030001ULL

typedef struct _SFB_USB_CONFIG_PROTOCOL SFB_USB_CONFIG_PROTOCOL;

typedef EFI_STATUS (EFIAPI *SFB_USB_CFG_START_CONTROLLER)(
  IN SFB_USB_CONFIG_PROTOCOL *This, IN UINT32 CoreNum, IN UINT32 ModeType);
typedef EFI_STATUS (EFIAPI *SFB_USB_CFG_STOP_CONTROLLER)(
  IN SFB_USB_CONFIG_PROTOCOL *This, IN UINT32 CoreNum, IN UINT32 ModeType);
typedef EFI_STATUS (EFIAPI *SFB_USB_CFG_CONFIG_USB)(
  IN SFB_USB_CONFIG_PROTOCOL *This, IN UINT32 ModeType, IN UINT32 CoreNum);
typedef EFI_STATUS (EFIAPI *SFB_USB_CFG_GET_VBUS_STATUS)(
  IN SFB_USB_CONFIG_PROTOCOL *This, IN UINT32 CoreNum, OUT UINT32 *VbusStatus);
typedef EFI_STATUS (EFIAPI *SFB_USB_CFG_ENABLE_VBUS)(
  IN SFB_USB_CONFIG_PROTOCOL *This, IN UINT32 CoreNum);
typedef EFI_STATUS (EFIAPI *SFB_USB_CFG_GET_SUPPORTED_MODE)(
  IN SFB_USB_CONFIG_PROTOCOL *This, IN UINT32 CoreNum, OUT UINT32 *ModeType);
typedef EFI_STATUS (EFIAPI *SFB_USB_CFG_GET_CORE_COUNT)(
  IN SFB_USB_CONFIG_PROTOCOL *This, OUT UINT32 *CoreCount);
typedef EFI_STATUS (EFIAPI *SFB_USB_CFG_GET_MAX_HOST_CORE)(
  IN SFB_USB_CONFIG_PROTOCOL *This, OUT UINT32 *MaxHostCoreNum);
typedef EFI_STATUS (EFIAPI *SFB_USB_CFG_SET_USB_CORE_MODE)(
  IN SFB_USB_CONFIG_PROTOCOL *This, IN UINT32 CoreIdx, IN UINT32 NewMode);

struct _SFB_USB_CONFIG_PROTOCOL {
  UINT64                        Revision;
  VOID                         *GetCoreBaseAddr;
  SFB_USB_CFG_CONFIG_USB        ConfigUsb;
  VOID                         *ResetUsb;
  VOID                         *GetUsbFnConfig;
  VOID                         *GetSSUsbFnConfig;
  VOID                         *GetUsbFnConnStatus;
  VOID                         *GetUsbHostConfig;
  SFB_USB_CFG_GET_MAX_HOST_CORE GetUsbMaxHostCoreNum;
  VOID                         *ExitUsbLibServices;
  SFB_USB_CFG_START_CONTROLLER  StartController;
  SFB_USB_CFG_STOP_CONTROLLER   StopController;
  VOID                         *EnterLPM;
  VOID                         *ExitLPM;
  VOID                         *ToggleUsbMode;
  SFB_USB_CFG_GET_CORE_COUNT    GetCoreCount;
  SFB_USB_CFG_GET_SUPPORTED_MODE GetSupUsbMode;
  UINT32                        CoreNum;
  UINT32                        ModeType;
  UINT8                         AlwaysConnected;
  SFB_USB_CFG_GET_VBUS_STATUS   GetUsbVbusStatus;
  SFB_USB_CFG_ENABLE_VBUS       UsbEnableVbus;
  /*
   * Tail through revision 3. Offsets verified against the vendor header by
   * canoe-usb/tools/offsets_probe.c: PollSSPhyTraining 0xa8 .. SetUsbCoreMode
   * 0xf0, sizeof 0xf8. Members are only present when Revision reaches the
   * value that introduced them, and the vendor ships NULL members even when
   * present (ExitUsbLibServices is NULL on the 2.5.1 build), so every call
   * site must check both Revision and the pointer.
   */
  VOID                         *PollSSPhyTraining;
  VOID                         *AdvanceSSCmplPattern;
  VOID                         *GetWoLState;      /* revision 2 */
  VOID                         *SetWoLState;      /* revision 2 */
  VOID                         *GetVariable;      /* revision 3 */
  VOID                         *SetVariable;      /* revision 3 */
  VOID                         *IsEudEnable;      /* revision 3 */
  VOID                         *SetUsbLoopback;   /* revision 3 */
  VOID                         *GetUsbCoreInfo;   /* revision 3 */
  SFB_USB_CFG_SET_USB_CORE_MODE SetUsbCoreMode;   /* revision 3 */
};

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
   * CoreCount, and cores the query failed for, are SFB_USB_INVALID_MODE. */
  UINT32  CoreModes[SFB_USB_CORE_MAX_NUM];
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
