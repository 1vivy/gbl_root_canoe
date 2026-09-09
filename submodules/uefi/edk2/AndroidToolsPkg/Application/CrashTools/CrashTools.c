/** @file
 *  CrashTools - force the device into Qualcomm 900e memory-debug mode.
 *
 *  Standalone companion to MdTools for the case where no table edit is
 *  needed (or to prove a trigger before trusting it in an edit+crash run).
 *  Launched from the ABL with `fastboot boot CrashTools.efi`.
 *
 *  Every trigger confirms first. A trigger that returns means the device
 *  did NOT go down - that survival is the finding, and the result screen
 *  says so. The EDL row is the control: the vendor ResetSystem path lands
 *  in Sahara 9008, proving the reset channel works while the fault rows
 *  aim at 900e.
 *
 *  Never shipped in the release packages (probe tooling, not an operator
 *  tool).
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <AndroidToolsUi.h>
#include <MdTable.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)  (sizeof (a) / sizeof ((a)[0]))
#endif

/* 2026-09-02 capture measurement for the expected= line. */
#define CT_EXPECT_TZ_ADDR  0xD85FF000ULL
#define CT_EXPECT_TZ_SIZE  0x3F3000ULL
#define CT_CRASH_PATTERN   0xDEADBEEFu

STATIC MD_TABLE_MAP mCtMap;
STATIC BOOLEAN      mCtScanned = FALSE;

STATIC
EFI_STATUS
CtEnsureScan (
  VOID
  )
{
  if (mCtScanned) {
    return (mCtMap.ArrayCount > 0) ? EFI_SUCCESS : EFI_NOT_FOUND;
  }
  mCtScanned = TRUE;
  return MdTableScan (&mCtMap);
}

/** Power fires, any volume key aborts. Returns TRUE to fire. */
STATIC
BOOLEAN
CtConfirm (
  IN CONST CHAR16 *What,
  IN CONST CHAR16 *Detail
  )
{
  AtUiBeginScreen (What, Detail);
  Print (L"Power = fire, Vol +/- = abort\r\n");
  AtUiEndScreen (NULL);
  return (BOOLEAN)(AtUiWaitForKey (0) == AtKeySelect);
}

STATIC
VOID
CtHoldResult (
  VOID
  )
{
  AtUiEndScreen (L"Power back");
  while (AtUiWaitForKey (0) != AtKeySelect) {
  }
}

STATIC
VOID
CtFindTargetsScreen (
  VOID
  )
{
  MD_REGION_ENTRY Entry;
  EFI_STATUS      Status;
  UINTN           ArrayIndex;
  UINTN           EntryIndex;
  UINT64          Hole;

  Status = CtEnsureScan ();
  AtUiBeginScreen (L"Crash Targets", EFI_ERROR (Status) ? L"scan failed"
                                                        : NULL);
  if (EFI_ERROR (Status)) {
    Print (L"table scan: %r (TZ target unknown)\r\n", Status);
  } else {
    Status = MdTableFindArray (&mCtMap, "TZ_DDR", &ArrayIndex);
    if (!EFI_ERROR (Status)) {
      Status = MdTableFindInArray (&mCtMap, ArrayIndex, "TZ_DDR",
                                   &EntryIndex, &Entry);
    }
    if (EFI_ERROR (Status)) {
      Print (L"TZ_DDR: not found in table\r\n");
    } else {
      Print (L"TZ_DDR @0x%lx size=0x%lx\r\n", Entry.Address, Entry.Size);
      Print (L"expected @0x%lx size=0x%lx %s\r\n",
             (UINT64)CT_EXPECT_TZ_ADDR, (UINT64)CT_EXPECT_TZ_SIZE,
             (Entry.Address == CT_EXPECT_TZ_ADDR &&
              Entry.Size == CT_EXPECT_TZ_SIZE) ? L"match" : L"DIFFERS");
    }
  }
  Status = MdFindUnmappedAddress (&Hole);
  if (EFI_ERROR (Status)) {
    Print (L"unmapped hole: none found (%r)\r\n", Status);
  } else {
    Print (L"unmapped hole candidate: 0x%lx\r\n", Hole);
  }
  CtHoldResult ();
}

STATIC
VOID
CtTzWriteScreen (
  VOID
  )
{
  MD_REGION_ENTRY Entry;
  EFI_STATUS      Status;
  UINTN           ArrayIndex;
  UINTN           EntryIndex;
  UINT32          Readback;

  Status = CtEnsureScan ();
  if (!EFI_ERROR (Status)) {
    Status = MdTableFindArray (&mCtMap, "TZ_DDR", &ArrayIndex);
  }
  if (!EFI_ERROR (Status)) {
    Status = MdTableFindInArray (&mCtMap, ArrayIndex, "TZ_DDR",
                                 &EntryIndex, &Entry);
  }
  if (EFI_ERROR (Status)) {
    AtUiReportStatus (L"Find TZ_DDR (run Find Targets first)", Status);
    return;
  }
  if (!CtConfirm (L"Crash: TZ Secure Write",
                  L"write to secure carveout; XPU should fault to 900e")) {
    return;
  }
  AtUiShowMessage (L"Firing TZ write...");
  MdTriggerWriteFault (Entry.Address + Entry.Size / 2, CT_CRASH_PATTERN,
                       &Readback);
  AtUiBeginScreen (L"Crash: TZ Secure Write", L"SURVIVED - no fault");
  Print (L"readback=0x%x (wrote 0x%x)\r\n", Readback, CT_CRASH_PATTERN);
  Print (L"XPU did not fault this path.\r\n");
  CtHoldResult ();
}

STATIC
VOID
CtUnmappedReadScreen (
  VOID
  )
{
  EFI_STATUS Status;
  UINT64     Hole;

  Status = MdFindUnmappedAddress (&Hole);
  if (EFI_ERROR (Status)) {
    AtUiReportStatus (L"Find unmapped address", Status);
    return;
  }
  if (!CtConfirm (L"Crash: Unmapped Read",
                  L"sync data abort; handler should drop to 900e")) {
    return;
  }
  AtUiShowMessage (L"Reading unmapped address...");
  MdTriggerReadFault (Hole);
  AtUiBeginScreen (L"Crash: Unmapped Read", L"SURVIVED - no abort");
  Print (L"0x%lx was readable after all (flat map?)\r\n", Hole);
  CtHoldResult ();
}

STATIC
VOID
CtBreakScreen (
  VOID
  )
{
  if (!CtConfirm (L"Crash: BRK Exception",
                  L"synchronous BRK; handler should drop to 900e")) {
    return;
  }
  AtUiShowMessage (L"Raising BRK...");
  MdTriggerBreak ();
  AtUiBeginScreen (L"Crash: BRK Exception", L"SURVIVED - handler resumed");
  CtHoldResult ();
}

STATIC
VOID
CtEdlScreen (
  VOID
  )
{
  if (!CtConfirm (L"Reboot to EDL 9008 (control)",
                  L"vendor ResetSystem EDL path; NOT the 900e dump")) {
    return;
  }
  AtUiShowMessage (L"Resetting to EDL...");
  MdTriggerEdlReset ();
  /* A return means the vendor path refused the reset data. */
  AtUiBeginScreen (L"Reboot to EDL", L"RETURNED - reset refused");
  CtHoldResult ();
}

EFI_STATUS
EFIAPI
CrashToolsEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  STATIC CONST CHAR16 *Items[] = {
    L"Find Targets (read-only)",
    L"Crash: TZ Secure Write (XPU)",
    L"Crash: Unmapped Read (sync abort)",
    L"Crash: BRK Exception",
    L"Reboot to EDL 9008 (control)",
    L"Back"
  };
  EFI_STATUS Status;
  UINTN      Selected;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  AtUiEnterMenu (L"Crash Tools");
  while (TRUE) {
    Status = AtUiRunMenu (L"Crash Tools", Items, ARRAY_SIZE (Items),
                          &Selected,
                          L"900e aim: fault rows; 9008 control: EDL row");
    if (EFI_ERROR (Status)) {
      continue;
    }
    switch (Selected) {
      case 0:
        CtFindTargetsScreen ();
        break;
      case 1:
        CtTzWriteScreen ();
        break;
      case 2:
        CtUnmappedReadScreen ();
        break;
      case 3:
        CtBreakScreen ();
        break;
      case 4:
        CtEdlScreen ();
        break;
      default:
        return EFI_SUCCESS;
    }
  }
}
