/** @file
 *  MdTools menu. The menu offers a bounded read-only report, one owned-buffer
 *  crash probe, and a resident shadow-target picker. Each attempt opens and
 *  flushes durable evidence before discovery or mutation begins.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
*/
#include <Uefi.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>
#include <AndroidToolsUi.h>

#include "MdTools.h"
#include "MdShadow.h"


STATIC CONST AT_REPORT_SOURCE mMdReports[] = {
  { L"Subsystem Map", MdBuildMapReport },
  { L"Region Detail", MdBuildRegionReport }
};


VOID
MdRunPathwayScreen (
  IN CONST MD_PATHWAY   *Pathway,
  IN MD_PATHWAY_INTRO_FN Intro OPTIONAL
  )
{
  EFI_STATUS Status;
  UINTN      Completed;
  UINTN      Index;

  Completed = 0;
  AtUiBeginScreen (Pathway->Name, L"Durable bounded minidump operation");
  Print (L"Evidence opens and flushes before discovery.\r\n");
  Print (L"Only SMEM item 602 plus BOOT/AOP arrays are read.\r\n");
  if (Pathway->Registers) {
    Print (L"The owned region points at THIS image's memory,\r\n");
    Print (L"so this pathway claims a slot and triggers\r\n");
    Print (L"collection before MdTools can exit.\r\n");
  }
  if (Intro != NULL) {
    Intro ();
  }
  Print (L"Rungs: %u\r\n", (UINT32)Pathway->RungCount);
  AtUiEndScreen (L"Power = walk, Vol +/- = cancel");
  if (AtUiWaitForKey (0) != AtKeySelect) {
    return;
  }

  AtUiShowMessage (L"Walking pathway...");
  Status = MdWalkPathway (Pathway, &Completed);

  AtUiBeginScreen (Pathway->Name,
                   EFI_ERROR (Status) ? L"Stopped" : L"Complete");
  Print (L"rungs completed: %u of %u\r\n", (UINT32)Completed,
         (UINT32)Pathway->RungCount);
  Print (L"walk status: %r\r\n", Status);
  /* Reaching the menu at all means no fault was taken, so nothing was
     collected. Never let this screen read as success for a registering
     pathway. */
  if (Pathway->Registers) {
    Print (L"NO FAULT TAKEN: nothing was collected this run\r\n");
    Print (L"the registration is void once MdTools exits\r\n");
  }
  Print (L"evidence: %s\\md-<rung>-<n>.txt\r\n", MD_EVIDENCE_DIR);
  Print (L"files are never overwritten; oldest pruned last\r\n");
  AtUiEndScreen (L"Power back");
  while (AtUiWaitForKey (0) != AtKeySelect) {
  }

  /* The report pathway also pages the same flushed report on the console. */
  if (Pathway->ShowReports && !EFI_ERROR (Status)) {
    for (Index = 0; Index < sizeof (mMdReports) / sizeof (mMdReports[0]);
         Index++) {
      AtUiShowReport (&mMdReports[Index]);
    }
  }
}

EFI_STATUS
EFIAPI
MdToolsEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  STATIC CONST CHAR16 *Footer =
    L"Evidence first; registrations live only until MdTools exits";
  CONST MD_PATHWAY   *Pathways;
  CONST CHAR16      **Items;
  EFI_STATUS          Status;
  UINTN               Count;
  UINTN               Index;
  UINTN               Selected;

  (VOID)ImageHandle;
  (VOID)SystemTable;

  Pathways = MdPathways (&Count);
  Items = AllocateZeroPool ((Count + 2) * sizeof (CHAR16 *));
  if (Items == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  for (Index = 0; Index < Count; Index++) {
    Items[Index] = Pathways[Index].Name;
  }
  Items[Count] = L"3 Shadow existing region + collection trigger";
  Items[Count + 1] = L"Back";

  AtUiEnterMenu (L"Minidump Tools");
  while (TRUE) {
    Status = AtUiRunMenu (L"Minidump Tools", Items, Count + 2, &Selected,
                          Footer);
    if (EFI_ERROR (Status)) {
      continue;
    }
    if (Selected == Count) {
      MdRunShadowTargetMenu ();
      continue;
    }
    if (Selected > Count) {
      FreePool (Items);
      return EFI_SUCCESS;
    }
    MdRunPathwayScreen (&Pathways[Selected], NULL);
  }
}

CONST AT_REPORT_SOURCE *
MdReportSources (
  OUT UINTN *Count
  )
{
  /* Menu and evidence files share one list so a section cannot appear on
     screen but be missing from the record. */
  *Count = sizeof (mMdReports) / sizeof (mMdReports[0]);
  return mMdReports;
}
