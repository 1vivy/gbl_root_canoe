/** @file
 *  UsbTools - standalone USB host-mode diagnostics for Canoe.
 *
 *  The census is passive and safe from any screen. The host-mode attempt is
 *  separately confirmed, writes controller state through the vendor
 *  UsbConfig protocol, and always restores device mode before returning.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <AndroidToolsUi.h>
#include "UsbTools.h"

STATIC CONST AT_REPORT_SOURCE mUtCensusReport = {
  L"USB Census",
  UtBuildCensusReport
};

STATIC BOOLEAN
UtConfirmHostAttempt (VOID)
{
  AT_KEY Key;

  /*
   * The menu row is selected with power. Drain queued input, then require
   * Volume Up so a repeated event from that still-held key can only cancel.
   */
  gBS->Stall (1000000);
  AtUiResetInput ();
  AtUiBeginScreen (L"Attempt USB Host Mode?", L"Writes controller state");
  Print (L"Flips the USB core to XHCI host mode, loads the\r\n");
  Print (L"staged driver stack (efisp\\usbhost), watches what\r\n");
  Print (L"binds, then restores device mode.\r\n\r\n");
  Print (L"The fastboot link dies the moment the core flips.\r\n");
  Print (L"Unplug the PC and attach the stick first.\r\n\r\n");
  Print (L"Every stage is printed as it runs; a fault leaves\r\n");
  Print (L"the stage name on this screen.\r\n");
  AtUiEndScreen (L"Vol+ run; Power/Vol- cancel");

  Key = AtUiWaitForKey (0);
  return (BOOLEAN)(Key == AtKeyUp);
}

STATIC VOID
UtDumpScreen (VOID)
{
  EFI_STATUS Status;

  Status = UtDumpToLogfs ();
  AtUiBeginScreen (L"Dump to logfs",
                   (Status == EFI_SUCCESS) ? L"Complete" : L"Failed");
  Print (L"logfs:\\UsbToolsDump.txt: %r\r\n", Status);
  if (!mUtAttemptRan) {
    Print (L"\r\n(no host-mode attempt ran this session)\r\n");
  }
  AtUiEndScreen (L"Power back");
  while (AtUiWaitForKey (0) != AtKeySelect) {
  }
}

EFI_STATUS
EFIAPI
UsbToolsEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  STATIC CONST CHAR16 *Items[] = {
    L"USB Census (read-only)",
    L"Attempt USB Host Mode",
    L"Dump Report to logfs",
    L"Back"
  };
  EFI_STATUS Status;
  UINTN      Selected;

  (VOID)SystemTable;
  AtUiEnterMenu (L"USB Tools");

  while (TRUE) {
    Status = AtUiRunMenu (L"USB Tools", Items,
                          sizeof (Items) / sizeof (Items[0]), &Selected,
                          L"Census is passive; the attempt restores device mode");
    if (EFI_ERROR (Status)) {
      continue;
    }

    switch (Selected) {
      case 0:
        AtUiShowReport (&mUtCensusReport);
        break;
      case 1:
        if (UtConfirmHostAttempt ()) {
          UtRunHostAttempt (ImageHandle);
        }
        break;
      case 2:
        UtDumpScreen ();
        break;
      default:
        return EFI_SUCCESS;
    }
  }
}
