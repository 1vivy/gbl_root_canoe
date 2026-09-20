/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef REBOOT_TARGET_LIB_H
#define REBOOT_TARGET_LIB_H

#include <Uefi.h>

typedef enum {
  RebootTargetFastbootd, RebootTargetBootloader, RebootTargetRecovery,
  RebootTargetSystem, RebootTargetCount
} REBOOT_TARGET;

/*
 * Canoe boot-once record in the Android BCB command field (misc LBA 0 bytes
 * [0,32)): NUL-terminated ASCII "canoe-once:<selector>". The 11-byte prefix
 * leaves 20 selector bytes plus the NUL. Selectors are 1-20 bytes from
 * [A-Za-z0-9._:-] and name a canoe.cfg entry id, "bls:<stem>", or "fastboot".
 *
 * ABL ignores this unknown command and still dispatches efisp, allowing BDS to
 * consume it. Bytes [32,...) (status/recovery/stage) are never changed, so
 * Android and vendor BCB semantics remain untouched.
 */
#define REBOOT_BOOT_ONCE_SELECTOR_MAX  20
#define REBOOT_BOOT_ONCE_SELECTOR_BYTES  (REBOOT_BOOT_ONCE_SELECTOR_MAX + 1)

/* Prepare the ordinary Android BCB command and return the Qualcomm reset reason.
 * Writes only the command field and flushes before success. No reset on failure. */
EFI_STATUS RebootTargetPrepare (REBOOT_TARGET Target, UINT8 *Reason);

/* Write a validated boot-once selector without resetting the device. */
EFI_STATUS RebootTargetBootOnceArm (IN CONST CHAR8 *Selector);

/*
 * Read a valid Canoe record and clear+flush its command field before returning
 * it. Found remains FALSE on no record, malformed data, or any clear failure.
 */
EFI_STATUS
RebootTargetBootOnceReadAndClear (
  OUT CHAR8   Selector[REBOOT_BOOT_ONCE_SELECTOR_BYTES],
  OUT BOOLEAN *Found
  );

/* Clear only a Canoe boot-once namespace record; preserve unrelated commands. */
EFI_STATUS RebootTargetBootOnceClear (VOID);

/* Standard Qualcomm RESET_PARAM ABI. Callers finish in-flight work first. */
VOID RebootTargetReset (UINT8 Reason);
#endif
