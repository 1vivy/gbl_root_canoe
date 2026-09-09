/** @file
 *  SurfaceTools - standalone UEFI attack-surface inventory for Canoe.
 *
 *  Passive views enumerate protocol, table, image and memory-map metadata. The
 *  active flow is separately confirmed, calls only a documented read-only
 *  allowlist, and writes its complete bounded policy readback to logfs.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <AndroidToolsUi.h>
#include "SurfaceInventory.h"
#include "SurfaceDump.h"

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
  AtUiBeginScreen (
    L"Run Read-only Policy Probe?",
    L"Up to seven getters; two TrustZone policy reads");
  Print (L"This writes only logfs:%s.\r\n", ST_POLICY_DUMP_PATH);
  Print (L"The complete returned policy buffer is written as hex.\r\n");
  Print (L"The probes do not change firmware or debug state.\r\n");
  Print (L"Firmware-reported state is not physical effectiveness.\r\n");
  Print (L"Broken firmware can still hang or crash the device.\r\n");
  AtUiEndScreen (L"Vol+ run; Power/Vol- cancel");

  Key = AtUiWaitForKey (0);
  return (BOOLEAN)(Key == AtKeyUp);
}

STATIC VOID
ShowPolicyDumpFailure (
  IN EFI_STATUS Status
  )
{
  AtUiBeginScreen (L"Policy Probe", L"Collected results; dump failed");
  Print (L"logfs:%s: %r\r\n", ST_POLICY_DUMP_PATH, Status);
  Print (L"The collected results will still be shown.\r\n");
  AtUiEndScreen (L"Power continue");
  while (AtUiWaitForKey (0) != AtKeySelect) {
  }
}

STATIC VOID
RunActiveProbes (VOID)
{
  AT_REPORT  Report;
  EFI_STATUS Status;

  ZeroMem (&Report, sizeof (Report));
  Status = StBuildProbeReport (&Report);
  if (EFI_ERROR (Status)) {
    AtReportFree (&Report);
    AtUiReportStatus (L"Read-only Policy Probe", Status);
    return;
  }

  Status = StDumpPolicyReport (&Report);
  if (EFI_ERROR (Status)) {
    ShowPolicyDumpFailure (Status);
  }

  AtUiShowBuiltReport (L"Policy Probe Results", &Report);
  AtReportFree (&Report);
}

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
  Items[ST_PASSIVE_REPORT_COUNT + 1] = L"Run Read-only Policy Probe";
  Items[ST_PASSIVE_REPORT_COUNT + 2] = L"Back";

  (VOID)ImageHandle;
  (VOID)SystemTable;
  AtUiEnterMenu (L"Surface Tools");

  while (TRUE) {
    Status = AtUiRunMenu (L"Surface Tools", Items,
                          sizeof (Items) / sizeof (Items[0]), &Selected,
                          L"Passive views; policy dump writes \\SurfacePolicy.log");
    if (EFI_ERROR (Status)) {
      continue;
    }

    if (Selected < ST_PASSIVE_REPORT_COUNT) {
      AtUiShowReport (&gStPassiveReports[Selected]);
      continue;
    }
    if (Selected == ST_PASSIVE_REPORT_COUNT) {
      DumpPassiveInventory ();
      continue;
    }
    if (Selected == ST_PASSIVE_REPORT_COUNT + 1) {
      if (ConfirmActiveProbes ()) {
        RunActiveProbes ();
      }
      continue;
    }
    return EFI_SUCCESS;
  }
}
