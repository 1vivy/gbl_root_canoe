/** @file
 *  LogTools menu for the platform UART-ring probes.
 *
 *  The first two rows only read memory and metadata. The dump row is the
 *  sole write path and writes only the requested report file on logfs.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <AndroidToolsUi.h>
#include "LogTools.h"

STATIC CONST AT_REPORT_SOURCE mLtReports[] = {
  { L"Info Block", LtBuildInfoReport },
  { L"Active Ring Census", LtBuildRingReport }
};

STATIC
VOID
LtDumpScreen (
  VOID
  )
{
  EFI_STATUS Status;

  Status = LtDumpToLogfs ();
  AtUiBeginScreen (L"Dump Log Probe", (Status == EFI_SUCCESS) ?
                   L"Complete" : L"Failed");
  Print (L"logfs:%s: %r\r\n", LT_DUMP_PATH, Status);
  Print (L"Only this report file is written.\r\n");
  AtUiEndScreen (L"Power back");
  while (AtUiWaitForKey (0) != AtKeySelect) {
  }
}

EFI_STATUS
EFIAPI
LogToolsEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  STATIC CONST CHAR16 *Items[] = {
    L"Info Block (read-only)",
    L"Active Ring Census (read-only)",
    L"Dump All Probes to logfs",
    L"Back"
  };
  EFI_STATUS Status;
  UINTN      Selected;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  AtUiEnterMenu (L"Log Tools");
  while (TRUE) {
    Status = AtUiRunMenu (L"Log Tools", Items,
                          sizeof (Items) / sizeof (Items[0]), &Selected,
                          L"Probes read memory; dump writes logfs report");
    if (EFI_ERROR (Status)) {
      continue;
    }
    if (Selected < sizeof (mLtReports) / sizeof (mLtReports[0])) {
      AtUiShowReport (&mLtReports[Selected]);
      continue;
    }
    if (Selected == sizeof (mLtReports) / sizeof (mLtReports[0])) {
      LtDumpScreen ();
      continue;
    }
    return EFI_SUCCESS;
  }
}

CONST AT_REPORT_SOURCE *
LtReportSources (
  OUT UINTN *Count
  )
{
  /* The menu and the logfs dump present the same probes in the same order;
     one list keeps a row from appearing on screen but not in the report. */
  *Count = sizeof (mLtReports) / sizeof (mLtReports[0]);
  return mLtReports;
}
