/** @file
 *  Raw payload handoff helpers shared by standalone AndroidToolsPkg loaders.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef AT_RAW_BOOT_H_
#define AT_RAW_BOOT_H_

#include <Uefi.h>

/* Read a whole file from the volume this image was loaded from, into pool
 * memory. Path is image-relative-absolute (leading backslash). */
EFI_STATUS
AtRawReadFile (
  IN  CONST CHAR16  *Path,
  OUT VOID          **Data,
  OUT UINTN         *Size
  );

/* Reserve an exact physical range. Reports EFI_NOT_FOUND when the firmware
 * will not give up that address. */
EFI_STATUS
AtRawReserve (
  IN EFI_PHYSICAL_ADDRESS  Base,
  IN UINTN                 Size
  );

/*
 * Leave UEFI and jump. Never returns on success. Sequence:
 *   1. gBS->SetWatchdogTimer (0, 0, 0, NULL)
 *   2. gBS->ExitBootServices (gImageHandle, MapKey) with the standard
 *      GetMemoryMap/retry-once-on-EFI_INVALID_PARAMETER loop
 *   3. WriteBackInvalidateDataCacheRange over every placed range
 *   4. ArmDisableCachesAndMmu ()
 *   5. ArmInvalidateInstructionCache ()
 *   6. branch to Entry with (Arg0, 0, 0, 0) in x0..x3
 * After step 2 nothing may allocate, print, or call a Boot Service.
 */
VOID
AtRawJump (
  IN EFI_PHYSICAL_ADDRESS          Entry,
  IN UINT64                        Arg0,
  IN CONST EFI_PHYSICAL_ADDRESS    *Ranges,
  IN CONST UINTN                   *Sizes,
  IN UINTN                         RangeCount
  ) __attribute__ ((noreturn));

#endif
