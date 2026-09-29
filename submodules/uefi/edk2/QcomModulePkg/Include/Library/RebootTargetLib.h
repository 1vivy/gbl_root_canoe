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
 * [0,32)): NUL-terminated ASCII "canoe-once:<selector>", optionally tagged as
 * "canoe-once:<selector>+<target>".
 *
 * The 11-byte prefix leaves 20 selector bytes plus the NUL. A target tag spends
 * the '+' delimiter, the tag and room for the terminator out of the same field,
 * so a tagged selector is capped at 32 - 11 - 1 - tag bytes - 1: 11 bytes for
 * "recovery" and 10 for "fastbootd". '+' is the delimiter precisely because it
 * is not a selector character, so splitting on the first '+' cannot be
 * ambiguous - a colon delimiter could not tell "bls:recovery" from a selector
 * named "recovery" that carries a tag.
 *
 * Selectors are 1-20 bytes from [A-Za-z0-9._:-] and name a canoe.cfg entry id,
 * "bls:<stem>", or "fastboot". A tag names one of the standard AOSP targets this
 * firmware writes through RebootTargetPrepare: "recovery" (boot-recovery) and
 * "fastbootd" (boot-fastboot). Bootloader is deliberately absent - it writes no
 * command, and its reset reason means nothing to a handoff that does not reset.
 * A tagged record is valid only for a managed Android ABL row: those commands
 * are the stock bootloader's contract, and every other row reaches those modes
 * by its own means.
 *
 * ABL ignores this unknown command and still dispatches efisp, allowing BDS to
 * consume it. Bytes [32,...) (status/recovery/stage) are never changed, so
 * Android and vendor BCB semantics remain untouched.
 */
#define REBOOT_BOOT_ONCE_SELECTOR_MAX  20
#define REBOOT_BOOT_ONCE_SELECTOR_BYTES  (REBOOT_BOOT_ONCE_SELECTOR_MAX + 1)

/* Tag carried by a boot-once record. None is the untagged form, byte-identical
 * to what earlier firmware wrote and still honoured here. */
typedef enum {
  RebootBootOnceTargetNone = 0,
  RebootBootOnceTargetRecovery,
  RebootBootOnceTargetFastbootd,
  RebootBootOnceTargetCount
} REBOOT_BOOT_ONCE_TARGET;

/* Prepare the ordinary Android BCB command and return the Qualcomm reset reason.
 * Writes only the command field and flushes before success. No reset on failure. */
EFI_STATUS RebootTargetPrepare (REBOOT_TARGET Target, UINT8 *Reason);

/* Inspect the Android BCB command without consuming or changing it. */
EFI_STATUS RebootTargetIsFastbootd (OUT BOOLEAN *Detected);

/* Write a validated boot-once selector without resetting the device. Target is
 * RebootBootOnceTargetNone for the plain record, or the tag of a record that
 * names a standard target. An oversized selector or an unknown tag is refused
 * with EFI_INVALID_PARAMETER before misc is read or written, so a refusal never
 * leaves a partial record behind. */
EFI_STATUS
RebootTargetBootOnceArm (
  IN CONST CHAR8             *Selector,
  IN REBOOT_BOOT_ONCE_TARGET  Target
  );

/*
 * Read a valid Canoe record and clear+flush its command field before returning
 * it. Target receives the tag, or RebootBootOnceTargetNone for the plain record.
 * Found remains FALSE on no record, malformed data, or any clear failure.
 */
EFI_STATUS
RebootTargetBootOnceReadAndClear (
  OUT CHAR8                    Selector[REBOOT_BOOT_ONCE_SELECTOR_BYTES],
  OUT REBOOT_BOOT_ONCE_TARGET *Target,
  OUT BOOLEAN                 *Found
  );

/* Clear only a Canoe boot-once namespace record; preserve unrelated commands. */
EFI_STATUS RebootTargetBootOnceClear (VOID);

/* Literal a tag writes into the record, or NULL for the untagged record. */
CONST CHAR8 *
RebootTargetBootOnceTagName (IN REBOOT_BOOT_ONCE_TARGET Target);

/* Standard target a tag names, FALSE when the tag has no standard form. */
BOOLEAN
RebootTargetBootOnceTagTarget (
  IN  REBOOT_BOOT_ONCE_TARGET  Target,
  OUT REBOOT_TARGET           *Reboot
  );

/* Standard Qualcomm RESET_PARAM ABI. Callers finish in-flight work first. */
VOID RebootTargetReset (UINT8 Reason);
#endif
