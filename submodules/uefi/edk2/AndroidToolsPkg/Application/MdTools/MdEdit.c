/** @file
 *  MdTools edit actions. Every action confirms first, edits DDR only, and
 *  ends on a result screen that says exactly what was changed or why not.
 *  The reboot-rebuild property of the table is the safety net: no edit
 *  survives a reset.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "MdTools.h"

/** Power confirms, any volume key cancels. Returns TRUE on confirm. */
STATIC
BOOLEAN
MdConfirm (
  IN CONST CHAR16 *Action
  )
{
  AT_KEY Key;

  AtUiBeginScreen (Action, L"RAM-only edit; reboot rebuilds the table");
  Print (L"Power = confirm, Vol +/- = cancel\r\n");
  AtUiEndScreen (NULL);
  Key = AtUiWaitForKey (0);
  return (BOOLEAN)(Key == AtKeySelect);
}

STATIC
VOID
MdHoldResult (
  VOID
  )
{
  AtUiEndScreen (L"Power back");
  while (AtUiWaitForKey (0) != AtKeySelect) {
  }
}

/** First array whose subsystem exists and does not require encryption. */
STATIC
EFI_STATUS
MdFindPlaintextArray (
  OUT UINTN *ArrayIndex
  )
{
  CONST MD_TABLE_MAP *Map;
  UINTN              Index;

  Map = MdCachedMap ();
  if (Map == NULL) {
    return EFI_NOT_STARTED;
  }
  for (Index = 0; Index < Map->ArrayCount; Index++) {
    if (Map->Arrays[Index].SubsystemToc != 0 &&
        Map->Arrays[Index].EncryptionRequired == 0) {
      *ArrayIndex = Index;
      return EFI_SUCCESS;
    }
  }
  return EFI_NOT_FOUND;
}

VOID
MdAppendAliasesScreen (
  VOID
  )
{
  MD_TABLE_MAP   *Map;
  MD_REGION_ENTRY Source;
  UINTN          ArrayIndex;
  UINTN          SourceArray;
  UINTN          SourceEntry;
  UINTN          Appended;
  EFI_STATUS     Status;
  EFI_STATUS     StatusX;
  UINT64         UefiAddr;
  UINT64         UefiSize;
  UINT64         XblAddr;
  UINT64         XblSize;

  Status = MdEnsureScan ();
  if (EFI_ERROR (Status)) {
    AtUiReportStatus (L"Table scan", Status);
    return;
  }
  if (!MdConfirm (L"Append UEFI/XBL log aliases")) {
    return;
  }
  Map = (MD_TABLE_MAP *)MdCachedMap ();

  Status = MdFindPlaintextArray (&ArrayIndex);
  if (EFI_ERROR (Status)) {
    AtUiBeginScreen (L"Append Aliases", L"Failed");
    Print (L"No subsystem with encryption_required=0 found.\r\n");
    Print (L"Run 'Clear Encrypt' first, or the alias would be\r\n");
    Print (L"encrypted like the original region.\r\n");
    MdHoldResult ();
    return;
  }

  /* The alias goes into the plaintext array no matter which array owns the
     original region - it inherits the target subsystem's policy. */
  Status = MdTableFindArray (Map, "UEFI_LOG", &SourceArray);
  if (!EFI_ERROR (Status)) {
    Status = MdTableFindInArray (Map, SourceArray, "UEFI_LOG", &SourceEntry,
                                 &Source);
  }
  if (!EFI_ERROR (Status)) {
    UefiAddr = Source.Address;
    UefiSize = Source.Size;
    Status = MdTableAppendRegion (Map, ArrayIndex, MD_ALIAS_UEFI,
                                  UefiAddr, UefiSize, &Appended);
  }
  StatusX = MdTableFindArray (Map, "XBL_LOG", &SourceArray);
  if (!EFI_ERROR (StatusX)) {
    StatusX = MdTableFindInArray (Map, SourceArray, "XBL_LOG", &SourceEntry,
                                  &Source);
  }
  if (!EFI_ERROR (StatusX)) {
    XblAddr = Source.Address;
    XblSize = Source.Size;
    StatusX = MdTableAppendRegion (Map, ArrayIndex, MD_ALIAS_XBL,
                                   XblAddr, XblSize, &Appended);
  }
  AtUiBeginScreen (L"Append Aliases",
                   (!EFI_ERROR (Status) && !EFI_ERROR (StatusX)) ?
                   L"Complete" : L"Partial/Failed");
  Print (L"plaintext subsystem: array %u\r\n", (UINT32)ArrayIndex);
  Print (L"%a -> %r\r\n", MD_ALIAS_UEFI, Status);
  Print (L"%a -> %r\r\n", MD_ALIAS_XBL, StatusX);
  Print (L"RAM only; next dump carries md_%a.BIN / md_%a.BIN\r\n",
         MD_ALIAS_UEFI, MD_ALIAS_XBL);
  MdHoldResult ();
}

VOID
MdClearEncryptionScreen (
  VOID
  )
{
  MD_TABLE_MAP *Map;
  EFI_STATUS   Status;
  UINTN        Index;
  UINTN        Cleared;
  UINT32       Previous;

  Status = MdEnsureScan ();
  if (EFI_ERROR (Status)) {
    AtUiReportStatus (L"Table scan", Status);
    return;
  }
  if (!MdConfirm (L"Clear encryption_required on log subsystems")) {
    return;
  }
  Map = (MD_TABLE_MAP *)MdCachedMap ();
  Cleared = 0;
  AtUiBeginScreen (L"Clear Encrypt", NULL);
  for (Index = 0; Index < Map->ArrayCount; Index++) {
    if (Map->Arrays[Index].SubsystemToc == 0 ||
        Map->Arrays[Index].EncryptionRequired == 0) {
      continue;
    }
    Status = MdTableSetEncryptionRequired (Map, Index, 0, &Previous);
    Print (L"array %u: encr_required %u -> %u (%r)\r\n", (UINT32)Index,
           Previous, Map->Arrays[Index].EncryptionRequired, Status);
    if (!EFI_ERROR (Status)) {
      Cleared++;
    }
  }
  if (Cleared == 0) {
    Print (L"no subsystem needed clearing (all already 0 or ToC-less)\r\n");
  }
  MdHoldResult ();
}

VOID
MdCrashTestScreen (
  VOID
  )
{
  CONST MD_TABLE_MAP *Map;
  MD_REGION_ENTRY    Entry;
  EFI_STATUS         Status;
  UINTN              ArrayIndex;
  UINTN              EntryIndex;
  UINT32             Readback;

  Status = MdEnsureScan ();
  if (EFI_ERROR (Status)) {
    AtUiReportStatus (L"Table scan", Status);
    return;
  }
  Map = MdCachedMap ();
  Status = MdTableFindArray (Map, "TZ_DDR", &ArrayIndex);
  if (!EFI_ERROR (Status)) {
    Status = MdTableFindInArray (Map, ArrayIndex, "TZ_DDR", &EntryIndex,
                                 &Entry);
  }
  if (EFI_ERROR (Status)) {
    AtUiReportStatus (L"Find TZ_DDR region", Status);
    return;
  }
  AtUiBeginScreen (L"Crash Test: TZ Write", NULL);
  Print (L"target: %a @0x%lx size=0x%lx\r\n", "TZ_DDR", Entry.Address,
         Entry.Size);
  Print (L"expected @0x%lx size=0x%lx %s\r\n", (UINT64)MD_EXPECT_TZ_ADDR,
         (UINT64)MD_EXPECT_TZ_SIZE,
         (Entry.Address == MD_EXPECT_TZ_ADDR && Entry.Size == MD_EXPECT_TZ_SIZE)
         ? L"match" : L"DIFFERS");
  Print (L"writes 0x%x to the secure carveout middle; an XPU\r\n",
         MD_CRASH_PATTERN);
  Print (L"refusal should fault the device into the 900e dump.\r\n");
  Print (L"Power = fire, Vol +/- = cancel\r\n");
  AtUiEndScreen (NULL);
  if (AtUiWaitForKey (0) != AtKeySelect) {
    return;
  }

  AtUiShowMessage (L"Firing TZ write...");
  MdTriggerWriteFault (Entry.Address + Entry.Size / 2, MD_CRASH_PATTERN,
                       &Readback);

  /* Reaching here means the device survived: the write did not fault. */
  AtUiBeginScreen (L"Crash Test: TZ Write", L"SURVIVED - no fault taken");
  Print (L"readback=0x%x (wrote 0x%x)\r\n", Readback, MD_CRASH_PATTERN);
  Print (L"The XPU did not fault this path; try another trigger.\r\n");
  MdHoldResult ();
}
