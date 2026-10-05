/** @file
 *  Interactive EUD diagnostic application.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <AndroidToolsUi.h>

#include "EudTools.h"

STATIC EFI_STATUS
EudBuildStatusSource (
  OUT AT_REPORT *Report
  )
{
  return EudBuildStatusReport (Report);
}

STATIC CONST AT_REPORT_SOURCE mStatusSource = {
  L"EUD Status",
  EudBuildStatusSource
};

STATIC BOOLEAN
EudConfirm (
  IN CONST CHAR16 *Title,
  IN CONST CHAR16 *Subtitle,
  IN CONST CHAR16 *Line1,
  IN CONST CHAR16 *Line2
  )
{
  AT_KEY Key;

  gBS->Stall (1000000);
  AtUiResetInput ();
  AtUiBeginScreen (Title, Subtitle);
  Print (L"%s\r\n", Line1);
  Print (L"%s\r\n", Line2);
  Print (L"A unique logfs evidence file is flushed first.\r\n");
  Print (L"SM8850-EUD minidump telemetry is armed only on SM8850.\r\n");
  AtUiEndScreen (L"Vol+ run; Power/Vol- cancel");
  Key = AtUiWaitForKey (0);
  return (BOOLEAN)(Key == AtKeyUp);
}

STATIC VOID
EudShowResult (
  IN CONST CHAR16 *Action,
  IN EFI_STATUS    Status
  )
{
  AtUiBeginScreen (Action, EFI_ERROR (Status) ? L"Stopped" : L"Complete");
  Print (L"status: %r\r\n", Status);
  Print (L"evidence: %s\\eud-<action>-<n>.txt\r\n", AT_EVIDENCE_DIR);
  Print (L"host enumeration remains an external observation.\r\n");
  AtUiEndScreen (L"Power back");
  while (AtUiWaitForKey (0) != AtKeySelect) {
  }
}

STATIC VOID
EudRunSecureProbeScreen (
  VOID
  )
{
  EFI_STATUS Status;

  if (!EudConfirm (
         L"Probe Secure EUD Gate?",
         L"Writes mode-manager 1, reads back, then restores",
         L"Calls TZ_IO_ACCESS_WRITE for the detected SoC profile.",
         L"Does not set the nonsecure EUD CSR or attach the hub.")) {
    return;
  }
  AtUiShowMessage (L"Running secure EUD gate probe...");
  Status = EudProbeSecureGate ();
  EudShowResult (L"Secure EUD Gate Probe", Status);
}

STATIC VOID
EudRunEnableScreen (
  IN BOOLEAN AttemptSecure
  )
{
  EFI_STATUS Status;

  if (!EudConfirm (
         AttemptSecure ? L"Enable EUD?" : L"Run Rejection Path?",
         AttemptSecure
         ? L"Secure write, then nonsecure path even on rejection"
         : L"Skip secure write; exercise only nonsecure controls",
         L"Programs OEM delay, UTMI, CSR, interrupt mask and attach-pet.",
         L"USB may disconnect, re-enumerate, hang, or enter crashdump.")) {
    return;
  }
  AtUiShowMessage (L"Enabling EUD; watch the host USB bus...");
  Status = EudEnablePath (AttemptSecure);
  EudShowResult (
    AttemptSecure ? L"EUD Enable Path" : L"EUD Rejection Path",
    Status
    );
}

STATIC VOID
EudRunComScreen (
  VOID
  )
{
  EFI_STATUS Status;

  if (!EudConfirm (
         L"Run EUD COM Test?",
         L"Ten-second bounded bidirectional test",
         L"Enables RX/TX indications and emits SOC-EUD OK.",
         L"Received data is recorded only; no command or SysRq executes.")) {
    return;
  }
  AtUiShowMessage (L"Polling EUD COM for ten seconds...");
  Status = EudRunComTest ();
  EudShowResult (L"EUD COM Test", Status);
}

STATIC VOID
EudRunRestoreScreen (
  VOID
  )
{
  EFI_STATUS Status;

  if (!EudConfirm (
         L"Restore Pre-tool EUD State?",
         L"Uses the exact first enable-path snapshot",
         L"Restores attach, mask, CSR, OEM delay and UTMI values.",
         L"Restores the secure bit only when readback shows it changed.")) {
    return;
  }
  AtUiShowMessage (L"Restoring captured EUD state...");
  Status = EudRestoreBaseline ();
  EudShowResult (L"Restore EUD State", Status);
}

EFI_STATUS
EFIAPI
EudToolsEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  STATIC CONST CHAR16 *Items[] = {
    L"Show live EUD + secure-gate status",
    L"Record status to crash-safe logfs evidence",
    L"Probe secure mode-manager write and restore",
    L"Enable EUD: secure attempt + rejection continuation",
    L"Enable EUD: nonsecure rejection path only",
    L"Run bounded bidirectional EUD COM test",
    L"Restore exact pre-tool EUD state",
    L"Back (leave current EUD state unchanged)"
  };
  EFI_STATUS Status;
  UINTN      Selected;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Status = EudSessionInitialize ();
  if (EFI_ERROR (Status)) {
    return Status;
  }
  AtUiEnterMenu (L"EUD Tools");
  while (TRUE) {
    Status = AtUiRunMenu (
               L"EUD Tools",
               Items,
               sizeof (Items) / sizeof (Items[0]),
               &Selected,
               L"Evidence first; COM RX never executes commands"
               );
    if (EFI_ERROR (Status)) {
      continue;
    }
    switch (Selected) {
    case 0:
      AtUiShowReport (&mStatusSource);
      break;
    case 1:
      AtUiShowMessage (L"Recording EUD status...");
      Status = EudRecordStatus ();
      EudShowResult (L"Record EUD Status", Status);
      break;
    case 2:
      EudRunSecureProbeScreen ();
      break;
    case 3:
      EudRunEnableScreen (TRUE);
      break;
    case 4:
      EudRunEnableScreen (FALSE);
      break;
    case 5:
      EudRunComScreen ();
      break;
    case 6:
      EudRunRestoreScreen ();
      break;
    default:
      if (!EudCanExit ()) {
        AtUiReportStatus (
          L"Minidump slot could not be released; reset before unloading",
          EFI_ACCESS_DENIED
          );
        break;
      }
      return EFI_SUCCESS;
    }
  }
}
