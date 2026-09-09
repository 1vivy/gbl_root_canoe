/** @file
 *  MdTools menu. Read-only rows build reports; edit rows confirm first and
 *  change DDR only. Nothing here writes to storage except the explicit
 *  logfs dump row.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <AndroidToolsUi.h>

#include "MdTools.h"

STATIC MD_TABLE_MAP mMdMap;
STATIC BOOLEAN      mMdScanned = FALSE;

STATIC CONST AT_REPORT_SOURCE mMdReports[] = {
  { L"Subsystem Map", MdBuildMapReport },
  { L"Region Detail", MdBuildRegionReport }
};

EFI_STATUS
MdEnsureScan (
  VOID
  )
{
  if (mMdScanned) {
    return (mMdMap.ArrayCount > 0) ? EFI_SUCCESS : EFI_NOT_FOUND;
  }
  mMdScanned = TRUE;
  return MdTableScan (&mMdMap);
}

CONST MD_TABLE_MAP *
MdCachedMap (
  VOID
  )
{
  return mMdScanned ? &mMdMap : NULL;
}

STATIC
VOID
MdDumpScreen (
  VOID
  )
{
  EFI_STATUS Status;

  Status = MdDumpToLogfs ();
  AtUiBeginScreen (L"Dump Minidump Probe", (Status == EFI_SUCCESS) ?
                   L"Complete" : L"Failed");
  Print (L"logfs:%s: %r\r\n", MD_DUMP_PATH, Status);
  Print (L"Only this report file is written.\r\n");
  AtUiEndScreen (L"Power back");
  while (AtUiWaitForKey (0) != AtKeySelect) {
  }
}

EFI_STATUS
EFIAPI
MdToolsEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  STATIC CONST CHAR16 *Items[] = {
    L"Subsystem Map (read-only scan)",
    L"Region Detail (read-only scan)",
    L"Dump Report to logfs",
    L"Append Log Aliases (plaintext subsys)",
    L"Clear Encrypt on Log Subsystems",
    L"Crash Test: TZ Write (fires dump)",
    L"Back"
  };
  EFI_STATUS Status;
  UINTN      Selected;
  UINTN      ReportRows;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  ReportRows = sizeof (mMdReports) / sizeof (mMdReports[0]);
  AtUiEnterMenu (L"Minidump Tools");
  while (TRUE) {
    Status = AtUiRunMenu (L"Minidump Tools", Items,
                          sizeof (Items) / sizeof (Items[0]), &Selected,
                          L"Scans read DDR; edits are RAM-only, reboot clears");
    if (EFI_ERROR (Status)) {
      continue;
    }
    if (Selected < ReportRows) {
      Status = MdEnsureScan ();
      if (EFI_ERROR (Status)) {
        AtUiReportStatus (L"Table scan (no region arrays found)", Status);
        continue;
      }
      AtUiShowReport (&mMdReports[Selected]);
      continue;
    }
    switch (Selected) {
      case 2:
        MdDumpScreen ();
        break;
      case 3:
        MdAppendAliasesScreen ();
        break;
      case 4:
        MdClearEncryptionScreen ();
        break;
      case 5:
        MdCrashTestScreen ();
        break;
      default:
        return EFI_SUCCESS;
    }
  }
}

CONST AT_REPORT_SOURCE *
MdReportSources (
  OUT UINTN *Count
  )
{
  /* Menu and logfs dump share one list so a row cannot appear on screen but
     be missing from the report. */
  *Count = sizeof (mMdReports) / sizeof (mMdReports[0]);
  return mMdReports;
}
