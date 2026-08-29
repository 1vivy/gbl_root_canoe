/** @file
 *  EFI_USB_PWR_CTRL_PROTOCOL, the Qualcomm USB power-control interface.
 *
 *  Mirrored from the vendor header (BOOT.MXF 2.5.1
 *  boot/QcomPkg/Include/Protocol/EFIUsbPwrCtrl.h): GUID at :38-39, member
 *  order at :490-507, call signatures at :286-436.
 *
 *  Note the ABI: these members take NO This pointer - the struct is a plain
 *  vtable of standalone functions, first argument is the port index.
 *
 *  Why this matters here: UsbConfig's UsbEnableVbus only delegates down to
 *  this protocol's SetVbusSourceEn, and the vendor's own host bring-up calls
 *  SetVbusSourceEn(port, FALSE) - it never raises bus power. On this target
 *  the power library also reports "Invalid Port Index = 0" at boot, so the
 *  delegation path is broken before it reaches here. Calling the protocol
 *  directly is the smallest test of whether the lever exists at all.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __USB_PWR_CTRL_H__
#define __USB_PWR_CTRL_H__

#include <Uefi.h>

#define UT_PWR_CTRL_REVISION_1_3  0x0000000000010003ULL
#define UT_PWR_CTRL_REVISION_1_4  0x0000000000010004ULL

typedef EFI_STATUS (EFIAPI *UT_PWR_GET_POWER_STATUS)(
  IN UINT8 PortIndex, OUT BOOLEAN *PowerState);
typedef EFI_STATUS (EFIAPI *UT_PWR_SET_POWER_SOURCE)(
  IN UINT8 PortIndex, IN BOOLEAN PowerEnable);

/*
 * Members this tool calls are typed; every other slot stays VOID * so the
 * vtable offsets remain exact. Order is load-bearing - it is read through a
 * pointer the platform handed us.
 */
typedef struct {
  UINT64                   Revision;
  VOID                    *GetHwInfo;
  UT_PWR_GET_POWER_STATUS  GetVbusDetectStatus;
  UT_PWR_GET_POWER_STATUS  GetVbusSrcOkStatus;
  VOID                    *GetHsUsbChgPortType;
  UT_PWR_GET_POWER_STATUS  GetUsbIDStatus;
  VOID                    *GetTypeCPortStatus;
  VOID                    *GetTypeCPortPDStatus;
  VOID                    *SetSinkPower;
  VOID                    *SetSourcePower;
  UT_PWR_SET_POWER_SOURCE  SetVbusSourceEn;
  UT_PWR_SET_POWER_SOURCE  SetVconnEn;
  VOID                    *SetPortEn;    /* revision 1.3 */
  VOID                    *GetPortEn;    /* revision 1.3 */
  VOID                    *GetPinAsgmt;  /* revision 1.4 */
} UT_USB_PWR_CTRL_PROTOCOL;

#endif
