/** @file
 * EFI_PMIC_SCHG_PROTOCOL, the Qualcomm switch-mode charger interface.
 *
 * Mirrored from the vendor header (BOOT.MXF 2.5.1
 * boot/QcomPkg/Include/Protocol/EFIPmicSchg.h): GUID at :88-89, revisions at
 * :72-83, member order at :2327-2418, signatures at :656-1936.
 *
 * This is the backend UsbPwrCtrlLib dispatches to for VBUS sourcing and
 * Type-C role. On this board that library's port table is pruned at
 * Detect_Hw, so every UsbPwrCtrl entry point answers Invalid Parameter -
 * but the charger protocol itself is published and reachable. Calling it
 * directly is how we get past a port table that has no valid entries.
 *
 * Members are declared in full order; only the ones this tool calls are
 * typed, the rest are VOID * so the offsets stay exact. None of them take a
 * This pointer - the PMIC device index is the first argument instead.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __USB_PMIC_SCHG_H__
#define __USB_PMIC_SCHG_H__

#include <Uefi.h>

#define UT_SCHG_REVISION_1  0x000000000001001Full

/* No PMIC device index established yet. */
#define UT_PMIC_NONE  0xFFFFFFFFu
#define UT_SCHG_REVISION_9  0x0000000000010027ull

/* EFIPmicSchg.h:349-357 */
typedef enum {
  UT_SCHG_OTG_ENABLED = 0,
  UT_SCHG_OTG_DISABLED,
  UT_SCHG_OTG_ERROR,
  UT_SCHG_OTG_EXECUTING_ENABLE_SEQ,
  UT_SCHG_OTG_EXECUTING_DISABLE_SEQ,
  UT_SCHG_OTG_STATUS_INVALID
} UT_SCHG_OTG_STATUS;

/* EFIPmicSchg.h:168-176. Selects whether the OTG boost is armed from the
 * command register or from the Type-C/RID state machine. With a pruned port
 * table the Type-C source never fires, so we ask for the command register. */
typedef enum {
  UT_SCHG_OTG_CFG_HICCUP_TIMER_SEL = 0,
  UT_SCHG_OTG_CFG_EN_SRC_CFG,
  UT_SCHG_OTG_CFG_ENABLE_IN_DEBUG_MODE,
  UT_SCHG_OTG_CFG_INVALID
} UT_SCHG_OTG_CFG;

/* EFIPmicSchg.h:521-527 */
typedef enum {
  UT_SCHG_PORT_ROLE_DRP = 0,
  UT_SCHG_PORT_ROLE_SNK,
  UT_SCHG_PORT_ROLE_SRC,
  UT_SCHG_PORT_ROLE_INVALID
} UT_SCHG_PORT_ROLE;

/* EFIPmicSchg.h:449-456 */
typedef enum {
  UT_SCHG_CONNECT_MODE_NONE = 0,
  UT_SCHG_CONNECT_MODE_DFP,
  UT_SCHG_CONNECT_MODE_UFP,
  UT_SCHG_CONNECT_MODE_INVALID
} UT_SCHG_CONNECT_MODE;

/* EFIPmicSchg.h:91 */
#define UT_SCHG_MAX_CHARGING_PORT  4

/* EFIPmicSchg.h:385-390. Names the valid PMIC indices outright, so the
 * device index never has to be guessed. */
typedef struct {
  UINT32  PmicIndex[UT_SCHG_MAX_CHARGING_PORT];
  UINT32  SlaveIndex[UT_SCHG_MAX_CHARGING_PORT];
  UINT32  ChargerCount;
} UT_SCHG_PMIC_INFO;

/* EFIPmicSchg.h:437-447. Field order is load-bearing; the three enums ahead
 * of the booleans are plain ints. */
typedef struct {
  UINT32   CcOutSts;
  UINT32   DfpCurrAdv;
  UINT32   UfpConnType;
  BOOLEAN  VbusSts;
  BOOLEAN  VbusErrSts;
  BOOLEAN  DebounceDoneSts;
} UT_SCHG_TYPEC_PORT_STATUS;

typedef struct {
  UINT64   Revision;
  VOID    *SchgInit;
  VOID    *EnableCharger;
  VOID    *GetPowerPath;
  VOID    *IsBatteryPresent;
  VOID    *GetChargerPortType;
  EFI_STATUS (EFIAPI *ConfigOtg) (IN UINT32 Pmic, IN UINT32 CfgType,
                                  IN BOOLEAN SetValue);
  EFI_STATUS (EFIAPI *SetOtgILimit) (IN UINT32 Pmic, IN UINT32 ImAmp);
  VOID    *EnableAfpMode;
  VOID    *SetInputPriority;
  VOID    *SetFccMaxCurrent;
  VOID    *SetFvMaxVoltage;
  EFI_STATUS (EFIAPI *EnableOtg) (IN UINT32 Pmic, IN BOOLEAN Enable);
  VOID    *UsbSuspend;
  VOID    *EnableJeita;
  EFI_STATUS (EFIAPI *GetOtgStatus) (IN UINT32 Pmic, OUT UINT32 *OtgStatus);
  EFI_STATUS (EFIAPI *UsbinValid) (IN UINT32 Pmic, OUT BOOLEAN *Valid);
  VOID    *SetUsbMaxCurrent;
  VOID    *ChgrSourceReinserted;
  VOID    *RerunAicl;
  VOID    *DumpPeripheral;
  VOID    *EnableChgWdog;
  VOID    *PetChgWdog;
  VOID    *GetChargingStatus;
  VOID    *DcinValid;
  VOID    *DcinSuspend;
  VOID    *SetDcinPower;
  VOID    *SchgExit;
  VOID    *SetUsbIclMode;
  VOID    *GetChgWdogStatus;
  VOID    *EnableHwJeita;
  VOID    *ToggleWipowerSDLatch;
  VOID    *SetDcinCurrent;
  VOID    *SetChargeCmdBit;
  EFI_STATUS (EFIAPI *SchgGetPmicInfo) (OUT UT_SCHG_PMIC_INFO *Info);
  VOID    *ConfigApsd;
  VOID    *ConfigHvDcp;
  VOID    *GetIclStatus;
  VOID    *SetVconn;
  EFI_STATUS (EFIAPI *GetPortState) (IN UINT32 Pmic,
                                     OUT UT_SCHG_TYPEC_PORT_STATUS *Status);
  EFI_STATUS (EFIAPI *GetConnectState) (IN UINT32 Pmic, OUT UINT32 *Mode);
  VOID    *GetHwJeitaStatus;
  VOID    *GetDCIrqStatus;
  VOID    *EnDebugAccessoryMode;
  VOID    *GetDAMConnectSts;
  VOID    *SetDAMIcl;
  VOID    *GetBattMissingStatus;
  EFI_STATUS (EFIAPI *GetActivePort) (OUT UINT8 *ActivePmicIndex);
  VOID    *SetTestModeDischarging;
  VOID    *GetLogCategoriesFromSdam;
  EFI_STATUS (EFIAPI *SchgGetChargerPmicIndex) (OUT UINT8 *ChargerPmicIndex);
  VOID    *SetShipMode;
  VOID    *GetValidPonReasons;
  VOID    *SetOffModeSrc;
  VOID    *SetOffModeSrcVbatThresh;
  VOID    *GetDvddReset;
  VOID    *SetOS;
  VOID    *SetMaxPwrReq;
  VOID    *GetMaxPwrReq;
  VOID    *SetUsbInput;
  VOID    *SetMaxPwrReqBattSts;
  VOID    *SetMaxPwrReqSkipChgReset;
  VOID    *SetPortEn;
  VOID    *EnableAicl;
  VOID    *EnableAiclPeriodicRerun;
  VOID    *SetChargerCfg;
  VOID    *SetChargerInhibit;
  VOID    *SetPonSWHardReset;
  VOID    *SetChgDxeLoadStatus;
  VOID    *SetChargerConfig;
  EFI_STATUS (EFIAPI *SetTypeCPortRole) (IN UINT32 Pmic, IN UINT32 RoleType);
  VOID    *GetPortEn;
  VOID    *SetRDOPDContractVoltage;
  VOID    *GetSavedActivePort;
  VOID    *SetGoodBattSOC;
  VOID    *GetDCINSupported;
  VOID    *SetSwThermalAdjustment;
  VOID    *SetChargingLimit;                    /* revision 1 */
  VOID    *CheckChargingLimit;                  /* revision 1 */
  VOID    *SetBatteryAuthentication;            /* revision 1 */
  VOID    *SetUnauthenticatedBatteryCharging;   /* revision 1 */
  VOID    *SetPowerState;                       /* revision 2 */
  VOID    *SetUefiChargingMode;                 /* revision 3 */
  VOID    *GetNumSerialBattery;                 /* revision 4 */
  VOID    *GetFakeBattStatus;                   /* revision 5 */
  VOID    *GetConvertedRidFromSdam;             /* revision 6 */
  VOID    *GetTadSdamInfo;                      /* revision 7 */
  VOID    *SetTadSdamInfo;                      /* revision 7 */
  VOID    *GetSBLNegotiatedInfo;                /* revision 8 */
  VOID    *SetPonFromLidEnable;                 /* revision 9 */
} UT_PMIC_SCHG_PROTOCOL;

#endif
