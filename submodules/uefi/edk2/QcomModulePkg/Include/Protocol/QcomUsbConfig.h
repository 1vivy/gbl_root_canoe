/** @file
 *  QCOM_USB_CONFIG_PROTOCOL, the resident UsbConfigDxe interface.
 *
 *  Mirrored from the vendor SDK header (BOOT.MXF 2.5.1
 *  sdk/QcomSdkPkg/Include/Protocol/EFIUsbConfig.h) and validated against the
 *  device's own UsbConfigDxe (revision 0x20001) by
 *  canoe-usb/tools/offsets_probe.c: PollSSPhyTraining at 0xa8 through
 *  SetUsbCoreMode at 0xf0, sizeof 0xf8.
 *
 *  Member order is load-bearing - the struct is only ever read through a
 *  pointer the platform handed us, so a reordered field is a call to the
 *  wrong function, not a compile error. The two enum-typed fields are UINT32
 *  and AlwaysConnected is UINT8; the vendor declares them as two enums plus
 *  UINT8, and both enums fit in int, so the widths agree on AArch64 LP64.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __QCOM_USB_CONFIG_H__
#define __QCOM_USB_CONFIG_H__

#define QCOM_USB_CORE_0            0u
#define QCOM_USB_CORE_1            1u
#define QCOM_USB_CORE_2            2u
#define QCOM_USB_CORE_MAX_NUM      6u

/*
 * QCOM_USB_MODE_TYPE carries two disjoint value sets in one enum, and mixing
 * them is a silent wrong answer rather than a type error.
 *
 * Controller-interface values are what StartController/StopController/
 * ConfigUsb take. Client-selection values are what GetSupUsbMode reports. A
 * capability test against GetSupUsbMode must use QCOM_USB_HOST_MODE, never
 * QCOM_USB_HOST_MODE_XHCI.
 */
#define QCOM_USB_HOST_MODE_XHCI    0x00000001u  /* interface: pass to Start */
#define QCOM_USB_DEVICE_MODE_SS    0x00000004u  /* interface: pass to Start */
#define QCOM_USB_HOST_MODE         0x00000008u  /* capability: GetSupUsbMode */
#define QCOM_USB_DEVICE_MODE       0x00000010u  /* capability: GetSupUsbMode */
#define QCOM_USB_DUAL_ROLE_MODE    0x00000020u  /* capability: GetSupUsbMode */
#define QCOM_USB_INVALID_MODE      0x00010000u

#define QCOM_USB_VBUS_DISABLED     0u

/* Vendor protocol revisions. A member added in revision N is only present
 * when Revision >= that value; reading further is a read past the end. The
 * vendor also ships NULL members even when present (ExitUsbLibServices is
 * NULL on the 2.5.1 build), so every call site checks both. */
#define QCOM_USB_CFG_REVISION_1    0x0000000000010006ULL
#define QCOM_USB_CFG_REVISION_2    0x0000000000020001ULL
#define QCOM_USB_CFG_REVISION_3    0x0000000000030001ULL

typedef struct _QCOM_USB_CONFIG_PROTOCOL QCOM_USB_CONFIG_PROTOCOL;

typedef EFI_STATUS (EFIAPI *QCOM_USB_CFG_START_CONTROLLER)(
  IN QCOM_USB_CONFIG_PROTOCOL *This, IN UINT32 CoreNum, IN UINT32 ModeType);
typedef EFI_STATUS (EFIAPI *QCOM_USB_CFG_STOP_CONTROLLER)(
  IN QCOM_USB_CONFIG_PROTOCOL *This, IN UINT32 CoreNum, IN UINT32 ModeType);
typedef EFI_STATUS (EFIAPI *QCOM_USB_CFG_CONFIG_USB)(
  IN QCOM_USB_CONFIG_PROTOCOL *This, IN UINT32 ModeType, IN UINT32 CoreNum);
typedef EFI_STATUS (EFIAPI *QCOM_USB_CFG_GET_VBUS_STATUS)(
  IN QCOM_USB_CONFIG_PROTOCOL *This, IN UINT32 CoreNum, OUT UINT32 *VbusStatus);
typedef EFI_STATUS (EFIAPI *QCOM_USB_CFG_ENABLE_VBUS)(
  IN QCOM_USB_CONFIG_PROTOCOL *This, IN UINT32 CoreNum);
typedef EFI_STATUS (EFIAPI *QCOM_USB_CFG_GET_SUPPORTED_MODE)(
  IN QCOM_USB_CONFIG_PROTOCOL *This, IN UINT32 CoreNum, OUT UINT32 *ModeType);
/* Vendor source (UsbConfigDxe.c): the count is a single byte. A wider
 * out-pointer leaves stack garbage in the upper bytes - and a bound check
 * against it reads as random. */
typedef EFI_STATUS (EFIAPI *QCOM_USB_CFG_GET_CORE_COUNT)(
  IN QCOM_USB_CONFIG_PROTOCOL *This, OUT UINT8 *CoreCount);
typedef EFI_STATUS (EFIAPI *QCOM_USB_CFG_TOGGLE_MODE)(
  IN QCOM_USB_CONFIG_PROTOCOL *This, IN UINT32 CoreNum);
typedef EFI_STATUS (EFIAPI *QCOM_USB_CFG_GET_MAX_HOST_CORE)(
  IN QCOM_USB_CONFIG_PROTOCOL *This, OUT UINT32 *MaxHostCoreNum);
typedef EFI_STATUS (EFIAPI *QCOM_USB_CFG_SET_USB_CORE_MODE)(
  IN QCOM_USB_CONFIG_PROTOCOL *This, IN UINT32 CoreIdx, IN UINT32 NewMode);
/* Vendor header: CoreType is the enum GetUsbHostConfig hands back and the
 * base address is UINTN. XhciPciEmulation uses exactly this pair to find
 * the controller it wraps, so the same pair locates the XHCI register
 * block for a read-only port inspection. */
typedef EFI_STATUS (EFIAPI *QCOM_USB_CFG_GET_CORE_BASE_ADDR)(
  IN QCOM_USB_CONFIG_PROTOCOL *This, IN UINT32 CoreType, OUT UINTN *BaseAddr);
typedef EFI_STATUS (EFIAPI *QCOM_USB_CFG_GET_USBHOST_CONFIG)(
  IN QCOM_USB_CONFIG_PROTOCOL *This, IN UINT32 Mode, IN UINT32 CoreNum,
  OUT UINT32 *CoreType);

struct _QCOM_USB_CONFIG_PROTOCOL {
  UINT64                         Revision;
  QCOM_USB_CFG_GET_CORE_BASE_ADDR GetCoreBaseAddr;
  QCOM_USB_CFG_CONFIG_USB        ConfigUsb;
  VOID                          *ResetUsb;
  VOID                          *GetUsbFnConfig;
  VOID                          *GetSSUsbFnConfig;
  VOID                          *GetUsbFnConnStatus;
  QCOM_USB_CFG_GET_USBHOST_CONFIG GetUsbHostConfig;
  QCOM_USB_CFG_GET_MAX_HOST_CORE GetUsbMaxHostCoreNum;
  VOID                          *ExitUsbLibServices;
  QCOM_USB_CFG_START_CONTROLLER  StartController;
  QCOM_USB_CFG_STOP_CONTROLLER   StopController;
  VOID                          *EnterLPM;
  VOID                          *ExitLPM;
  QCOM_USB_CFG_TOGGLE_MODE       ToggleUsbMode;
  QCOM_USB_CFG_GET_CORE_COUNT    GetCoreCount;
  QCOM_USB_CFG_GET_SUPPORTED_MODE GetSupUsbMode;
  UINT32                         CoreNum;
  UINT32                         ModeType;
  UINT8                          AlwaysConnected;
  QCOM_USB_CFG_GET_VBUS_STATUS   GetUsbVbusStatus;
  QCOM_USB_CFG_ENABLE_VBUS       UsbEnableVbus;
  /* Revision-gated tail; see the revision note above. */
  VOID                          *PollSSPhyTraining;
  VOID                          *AdvanceSSCmplPattern;
  VOID                          *GetWoLState;      /* revision 2 */
  VOID                          *SetWoLState;      /* revision 2 */
  VOID                          *GetVariable;      /* revision 3 */
  VOID                          *SetVariable;      /* revision 3 */
  VOID                          *IsEudEnable;      /* revision 3 */
  VOID                          *SetUsbLoopback;   /* revision 3 */
  VOID                          *GetUsbCoreInfo;   /* revision 3 */
  QCOM_USB_CFG_SET_USB_CORE_MODE SetUsbCoreMode;   /* revision 3 */
};

extern EFI_GUID gQcomUsbConfigProtocolGuid;

#endif
