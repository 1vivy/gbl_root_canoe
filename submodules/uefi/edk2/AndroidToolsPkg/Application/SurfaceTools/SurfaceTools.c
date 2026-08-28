/** @file
 *  SurfaceTools - standalone UEFI attack-surface inventory for Canoe.
 *
 *  Passive views enumerate protocol, table, image and memory-map metadata. The
 *  active view is separately confirmed and calls only a short documented
 *  read-only allowlist; it never writes storage, variables, fuses or USB state.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <AndroidToolsUi.h>
#include "SurfaceInventory.h"
#include "SurfaceDump.h"

#define ST_PAGE_ROWS  10u


STATIC VOID
ShowReport (
  IN CONST ST_REPORT_SOURCE *Source
  )
{
  ST_REPORT Report;
  EFI_STATUS Status;
  UINTN Start;
  UINTN End;
  UINTN Index;
  AT_KEY Key;
  CHAR16 Subtitle[64];

  Status = Source->Builder (&Report);
  if (EFI_ERROR (Status)) {
    AtUiReportStatus (Source->Title, Status);
    return;
  }

  Start = 0;
  while (TRUE) {
    End = Start + ST_PAGE_ROWS;
    if (End > Report.Count) {
      End = Report.Count;
    }
    UnicodeSPrint (Subtitle, sizeof (Subtitle), L"Rows %Lu-%Lu of %Lu%s",
                   (UINT64)((Report.Count == 0) ? 0 : Start + 1),
                   (UINT64)End, (UINT64)Report.Count,
                   Report.Truncated ? L" (truncated)" : L"");
    AtUiBeginScreen (Source->Title, Subtitle);
    for (Index = Start; Index < End; Index++) {
      Print (L"%s\r\n", Report.Rows[Index].Text);
    }
    AtUiEndScreen (L"Vol+/- page, power back");

    Key = AtUiWaitForKey (0);
    if (Key == AtKeySelect) {
      break;
    }
    if (Key == AtKeyUp) {
      Start = StMovePage (Start, Report.Count, ST_PAGE_ROWS, FALSE);
    } else if (Key == AtKeyDown) {
      Start = StMovePage (Start, Report.Count, ST_PAGE_ROWS, TRUE);
    }
  }
  StReportFree (&Report);
}

STATIC VOID
DumpPassiveInventory (VOID)
{
  EFI_STATUS Status;

  Status = StDumpPassiveInventory ();
  AtUiBeginScreen (L"Dump Passive Inventory",
                   (Status == EFI_SUCCESS) ? L"Complete" : L"Failed");
  Print (L"logfs:%s: %r\r\n", ST_DUMP_PATH, Status);
  AtUiEndScreen (L"Power back");
  while (AtUiWaitForKey (0) != AtKeySelect) {
  }
}

STATIC BOOLEAN
ConfirmActiveProbes (VOID)
{
  AT_KEY Key;

  /*
   * The menu row is selected with power. Drain queued input, then require
   * Volume Up so a repeated event from that still-held key can only cancel.
   */
  gBS->Stall (1000000);
  AtUiResetInput ();
  AtUiBeginScreen (L"Run Read-only Probes?", L"Explicit vendor calls");
  Print (L"CPU maximum index; TrustZone version (TZ query; no write);\r\n");
  Print (L"secure/boot state; Keymaster status (may contact TA).\r\n");
  Print (L"Values are firmware-reported and may be a Canoe\r\n");
  Print (L"managed projection, not raw hardware state.\r\n\r\n");
  Print (L"These calls write no state. A broken OEM implementation\r\n");
  Print (L"can still hang or crash the device.\r\n");
  AtUiEndScreen (L"Vol+ run all five; Power/Vol- cancel");

  Key = AtUiWaitForKey (0);
  return (BOOLEAN)(Key == AtKeyUp);
}

STATIC CONST ST_REPORT_SOURCE mActiveReport = {
  L"Active Probe Results",
  StBuildProbeReport
};

EFI_STATUS
EFIAPI
SurfaceToolsEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  CONST CHAR16 *Items[ST_PASSIVE_REPORT_COUNT + 3];
  EFI_STATUS Status;
  UINTN Selected;
  UINTN Index;

  for (Index = 0; Index < ST_PASSIVE_REPORT_COUNT; Index++) {
    Items[Index] = gStPassiveReports[Index].Title;
  }
  Items[ST_PASSIVE_REPORT_COUNT] = L"Dump Passive Inventory to logfs";
  Items[ST_PASSIVE_REPORT_COUNT + 1] = L"Run Read-only Active Probes";
  Items[ST_PASSIVE_REPORT_COUNT + 2] = L"Back";

  (VOID)ImageHandle;
  (VOID)SystemTable;
  AtUiEnterMenu (L"Surface Tools");

  while (TRUE) {
    Status = AtUiRunMenu (L"Surface Tools", Items,
                          sizeof (Items) / sizeof (Items[0]), &Selected,
                          L"Passive views; dump writes logfs");
    if (EFI_ERROR (Status)) {
      continue;
    }

    if (Selected < ST_PASSIVE_REPORT_COUNT) {
      ShowReport (&gStPassiveReports[Selected]);
      continue;
    }
    if (Selected == ST_PASSIVE_REPORT_COUNT) {
      DumpPassiveInventory ();
      continue;
    }
    if (Selected == ST_PASSIVE_REPORT_COUNT + 1) {
      if (ConfirmActiveProbes ()) {
        ShowReport (&mActiveReport);
      }
      continue;
    }
    return EFI_SUCCESS;
  }
}
