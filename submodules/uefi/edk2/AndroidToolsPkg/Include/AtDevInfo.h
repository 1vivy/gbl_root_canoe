/** @file
 *  DeviceInfo struct and rollback-index helpers, ported from the r32 tree
 *  (edk2-uefi.lnx.6.0.r32 QcomModulePkg/Include/Library/DeviceInfo.h and
 *  Library/BootLib/DeviceInfo.c). The rollback index array is the per-partition
 *  AVB anti-rollback counter; it lives inside the DeviceInfo blob that the
 *  Verified Boot protocol reads/writes from the persist partition.
 *
 *  The struct is copied verbatim so its on-disk layout matches what the
 *  platform DXE wrote. It is built WITHOUT AUTO_VIRT_ABL; if the target
 *  platform was built with AUTO_VIRT_ABL, define it here to match.
 *
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __AT_DEV_INFO_H__
#define __AT_DEV_INFO_H__

#include <Uefi.h>
#include "AtVerifiedBoot.h"

#define DEVICE_MAGIC       "ANDROID-BOOT!"
#define DEVICE_MAGIC_SIZE  13
#define MAX_VERSION_LEN    64
#define MAX_VB_PARTITIONS  32
#define MAX_USER_KEY_SIZE  2048
#define MAX_NAME_SIZE      56
#define MAX_VALUE_SIZE     32
#define MAX_ENTRY_SIZE     8
#define MAX_AUDIO_FW_LENGTH 16
#define DICE_KM_FRS_SIZE   32
#define DICE_HIDDEN_SIZE   64

typedef struct {
  UINT16  in_use;
  UINT16  name_size;
  UINT16  value_size;
  UINT8   name[MAX_NAME_SIZE];
  UINT8   value[MAX_VALUE_SIZE];
} persistent_value_type;

typedef struct device_info {
  CHAR8 magic[DEVICE_MAGIC_SIZE];
#ifdef AUTO_VIRT_ABL
  BOOLEAN IsResetDeviceState;
  CHAR8 Type;                       /* 0 = UNLOCK, 1 = UNLOCK_CRITICAL */
#endif
  BOOLEAN is_unlocked;
  BOOLEAN is_unlock_critical;
  BOOLEAN is_charger_screen_enabled;
  CHAR8 bootloader_version[MAX_VERSION_LEN];
  CHAR8 radio_version[MAX_VERSION_LEN];
  BOOLEAN verity_mode;              /* TRUE = enforcing, FALSE = logging */
  UINT32 user_public_key_length;
  CHAR8 user_public_key[MAX_USER_KEY_SIZE];
  UINT64 rollback_index[MAX_VB_PARTITIONS];
  persistent_value_type persistent_value[MAX_ENTRY_SIZE];
  UINTN GoldenSnapshot;
  CHAR8 AudioFramework[MAX_AUDIO_FW_LENGTH];
  UINT8 FdrFlag;
  UINT32 Km_frs_sec_len;
  UINT8 Km_frs_sec[DICE_KM_FRS_SIZE];
  UINT32 Dice_frs_len;
  UINT8 Dice_frs[DICE_HIDDEN_SIZE];
} DeviceInfo;

/**
  Read the whole DeviceInfo blob from the persist partition via the Verified
  Boot protocol. Returns EFI_SUCCESS and fills DevInfo.
**/
EFI_STATUS
AtDevInfoRead (
  OUT DeviceInfo *DevInfo
  );

/**
  Write the whole DeviceInfo blob back to the persist partition.
**/
EFI_STATUS
AtDevInfoWrite (
  IN CONST DeviceInfo *DevInfo
  );

/**
  Read one rollback-index slot. Loc is 0..MAX_VB_PARTITIONS-1. DevInfo must have
  been loaded first (via AtDevInfoRead).
**/
EFI_STATUS
AtReadRollbackIndex (
  IN  CONST DeviceInfo *DevInfo,
  IN  UINT32            Loc,
  OUT UINT64            *RollbackIndex
  );

/**
  Zero every rollback-index slot in DevInfo in memory. Caller persists with
  AtDevInfoWrite.
**/
VOID
AtClearRollbackIndex (
  IN OUT DeviceInfo *DevInfo
  );

#endif /* __AT_DEV_INFO_H__ */
