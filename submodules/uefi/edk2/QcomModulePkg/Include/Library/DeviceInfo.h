/** @file
 *  Canonical DeviceInfo layout shared by Qualcomm runtime code and AndroidTools.
 *
 *  This is the byte-exact persist blob used by Verified Boot. The rollback
 *  index array is the per-partition AVB anti-rollback counter. It is built
 *  without AUTO_VIRT_ABL; define AUTO_VIRT_ABL when matching a platform built
 *  with that option.
 *
 *  Source: edk2-uefi.lnx.6.0.r32 QcomModulePkg/Include/Library/DeviceInfo.h
 *  and Library/BootLib/DeviceInfo.c.
 *
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __DEVICE_INFO_H__
#define __DEVICE_INFO_H__

#include <Uefi.h>
#include <stddef.h>

#define DEVICE_MAGIC        "ANDROID-BOOT!"
#define DEVICE_MAGIC_SIZE   13
#define MAX_VERSION_LEN     64
#define MAX_VB_PARTITIONS   32
#define MAX_USER_KEY_SIZE   2048
#define MAX_NAME_SIZE       56
#define MAX_VALUE_SIZE      32
#define MAX_ENTRY_SIZE      8
#define MAX_AUDIO_FW_LENGTH 16
#define DICE_KM_FRS_SIZE    32
#define DICE_HIDDEN_SIZE    64

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
_Static_assert (sizeof (persistent_value_type) == 94,
                "persistent_value_type ABI size");
_Static_assert (offsetof (persistent_value_type, name) == 6,
                "persistent_value_type name offset");
_Static_assert (offsetof (persistent_value_type, value) == 62,
                "persistent_value_type value offset");

#ifndef AUTO_VIRT_ABL
_Static_assert (offsetof (DeviceInfo, is_unlocked) == 13,
                "DeviceInfo unlock offset");
_Static_assert (offsetof (DeviceInfo, is_unlock_critical) == 14,
                "DeviceInfo critical-unlock offset");
_Static_assert (offsetof (DeviceInfo, rollback_index) == 2200,
                "DeviceInfo rollback offset");
_Static_assert (offsetof (DeviceInfo, persistent_value) == 2456,
                "DeviceInfo persistent-value offset");
_Static_assert (offsetof (DeviceInfo, GoldenSnapshot) == 3208,
                "DeviceInfo snapshot offset");
_Static_assert (offsetof (DeviceInfo, FdrFlag) == 3232,
                "DeviceInfo FDR offset");
_Static_assert (offsetof (DeviceInfo, Km_frs_sec) == 3240,
                "DeviceInfo KM FRS offset");
_Static_assert (offsetof (DeviceInfo, Dice_frs) == 3276,
                "DeviceInfo DICE FRS offset");
_Static_assert (sizeof (DeviceInfo) == 3344, "DeviceInfo ABI size");
#endif


#endif /* __DEVICE_INFO_H__ */
