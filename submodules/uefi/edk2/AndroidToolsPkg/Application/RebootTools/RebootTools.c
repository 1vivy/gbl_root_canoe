/** @file
 *  RebootTools - a standalone UEFI tool launched from the super-fastboot boot
 *  menu. Offers four reboot targets (Fastbootd, Bootloader, Recovery, System).
 *
 *  RebootDevice and the misc-partition BCB write are ported from the r32 tree
 *  (ShutdownServices.c / Recovery.c) but trimmed to the self-contained paths:
 *  the reset is a single gRT->ResetSystem call, and the misc partition is found
 *  by its type GUID via LocateHandleBuffer rather than the full GPT enumerator.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>

#include <Library/RebootTargetLib.h>
#include "AndroidToolsUi.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)  (sizeof (a) / sizeof ((a)[0]))
#endif

/* ---- menu --------------------------------------------------------------- */

EFI_STATUS
EFIAPI
RebootToolsEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  STATIC CONST CHAR16 *Items[] = {
    L"Reboot to Fastbootd",
    L"Reboot to Bootloader",
    L"Reboot to Recovery",
    L"Reboot to System",
    L"Back",
  };
  UINTN      Sel;
  EFI_STATUS Status;

  /*
   * The power press that selected us in the super-fastboot menu is often still
   * held when we start; drain it (after a release delay) so it cannot confirm
   * the first entry the instant the menu appears.
   */
  AtUiEnterMenu (L"Reboot Tools");

  while (TRUE) {
    Status = AtUiRunMenu (L"Reboot Tools", Items, ARRAY_SIZE (Items), &Sel,
                          L"Vol+/- move, power select");
    if (EFI_ERROR (Status)) {
      continue;
    }

    if (Sel == RebootTargetCount) { return EFI_SUCCESS; }
    if (Sel < RebootTargetCount) {
      UINT8 Reason;
      Status = RebootTargetPrepare ((REBOOT_TARGET)Sel, &Reason);
      if (EFI_ERROR (Status)) { AtUiReportStatus (L"Prepare reboot target", Status); continue; }
      AtUiShowMessage (L"Restarting phone...");
      RebootTargetReset (Reason);
      AtUiReportStatus (L"Restart returned", EFI_DEVICE_ERROR);
    }
  }

  return EFI_SUCCESS;
}
