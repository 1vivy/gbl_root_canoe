/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef REBOOT_TARGET_LIB_H
#define REBOOT_TARGET_LIB_H
#include <Uefi.h>
typedef enum {
  RebootTargetFastbootd, RebootTargetBootloader, RebootTargetRecovery,
  RebootTargetSystem, RebootTargetCount
} REBOOT_TARGET;
/* Prepare the ordinary Android BCB command and return the Qualcomm reset reason.
 * Writes only the command field and flushes before success. No reset on failure. */
EFI_STATUS RebootTargetPrepare (REBOOT_TARGET Target, UINT8 *Reason);
/* Standard Qualcomm RESET_PARAM ABI. Callers finish in-flight work first. */
VOID RebootTargetReset (UINT8 Reason);
#endif
