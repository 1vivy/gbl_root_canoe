/** @file
 *  Reboot and recovery-message helpers, ported from the r32 tree
 *  (edk2-uefi.lnx.6.0.r32 QcomModulePkg: Library/FastbootLib/ShutdownServices.c
 *  for RebootDevice, and Include/Library/{Recovery.h,EFIResetReason.h} for the
 *  reset data layout, BCB strings and RecoveryMessage struct).
 *
 *  RebootDevice calls gRT->ResetSystem with a RESET_PARAM reset data buffer
 *  whose Bdata byte carries the reason. The misc-partition BCB write finds the
 *  misc partition by its type GUID (installed as a protocol on the partition
 *  handle by the platform partition driver) and writes the bootloader_message
 *  at offset 0, exactly as r32's WriteRecoveryMessage does on UFS.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __AT_REBOOT_H__
#define __AT_REBOOT_H__

#include <Uefi.h>

/* Reset reason bytes (r32 RebootReasonType). */
#define NORMAL_MODE    0x0
#define RECOVERY_MODE  0x1
#define FASTBOOT_MODE  0x2

/* Reset data parameter string (r32 STR_RESET_PARAM). */
#define AT_RESET_PARAM  L"RESET_PARAM"

/** Reset data buffer passed to gRT->ResetSystem (r32 ResetDataType). */
typedef struct {
  CHAR16 DataBuffer[12];
  UINT8  Bdata;
} __attribute__ ((packed, aligned (2))) AT_RESET_DATA;

/** Bootloader control message written to the misc partition (r32 RecoveryMessage,
 *  non-AUTO_VIRT_ABL layout). **/
struct RecoveryMessage {
  CHAR8 Command[32];
  CHAR8 Status[32];
  CHAR8 Recovery[768];
};

#define AT_RECOVERY_BOOT_RECOVERY  "boot-recovery"
#define AT_RECOVERY_BOOT_FASTBOOT  "boot-fastboot"

/**
  Reboot the device. Reason is one of NORMAL_MODE / RECOVERY_MODE / FASTBOOT_MODE.
  Does not return on success.
**/
VOID
AtRebootDevice (
  IN UINT8 Reason
  );

/**
  Write a bootloader control command to the misc partition (BCB). Command is a
  NUL-terminated ASCII string such as AT_RECOVERY_BOOT_RECOVERY.
**/
EFI_STATUS
AtWriteRecoveryMessage (
  IN CHAR8 *Command
  );

#endif /* __AT_REBOOT_H__ */
