/** @file
 *  MdTools report builders. Both reports read only the session-cached scan
 *  (MdCachedMap); the scan itself is read-only DDR.
 *
 *  The map report prints the subsystem policy words the whole tool exists
 *  for - encryption_required per subsystem - plus the G-ToC reconstruction.
 *  The region report lists every walked entry, with expected=/found lines
 *  for the measurements taken from the 2026-09-02 captures.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>

#include "MdTools.h"

#define MD_REPORT_ROWS  384u

STATIC
VOID
MdAsciiName (
  IN  CONST CHAR8 *Name,
  OUT CHAR16      *Wide,
  IN  UINTN       WideChars
  )
{
  UINTN Index;

  for (Index = 0; Index + 1 < WideChars && Index < MD_REGION_NAME_LEN &&
       Name[Index] != '\0'; Index++) {
    Wide[Index] = (Name[Index] >= 0x20 && Name[Index] <= 0x7e)
                  ? (CHAR16)Name[Index] : L'?';
  }
  Wide[Index] = L'\0';
}

EFI_STATUS
MdBuildMapReport (
  OUT AT_REPORT *Report
  )
{
  CONST MD_TABLE_MAP *Map;
  UINTN              Index;
  CHAR16             First[MD_REGION_NAME_LEN + 1];
  CHAR16             Last[MD_REGION_NAME_LEN + 1];
  EFI_STATUS         Status;

  Map = MdCachedMap ();
  if (Map == NULL) {
    return EFI_NOT_STARTED;
  }
  Status = AtReportInit (Report, MD_REPORT_ROWS);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  AtReportAdd (Report, L"scan: %lu MB in %us, anchors %u/%u",
               Map->BytesScanned / (1024 * 1024), Map->ScanSeconds,
               (UINT32)Map->AnchorHits, Map->AnchorTotal);
  AtReportAdd (Report, L"anchors hit mask=0x%x (bit0 XBL_LOG,1 UEFI_LOG,2 TZ_DDR,3 CPUCP_MISC_D,4 SMEMINFO)",
               Map->AnchorMask);
  AtReportAdd (Report, L"region arrays found: %u (expected>=2)",
               (UINT32)Map->ArrayCount);
  if (Map->ScanTruncated) {
    AtReportAdd (Report, L"WARNING: scan budget exhausted; coverage partial");
  }

  for (Index = 0; Index < Map->ArrayCount; Index++) {
    CONST MD_REGION_ARRAY *Array = &Map->Arrays[Index];
    MdAsciiName (Array->FirstName, First, sizeof (First) / sizeof (CHAR16));
    MdAsciiName (Array->LastName, Last, sizeof (Last) / sizeof (CHAR16));
    AtReportAdd (Report, L"array %u: base=0x%lx count=%u anchors=%u",
                 (UINT32)Index, (UINT64)Array->Base, (UINT32)Array->Count,
                 (UINT32)Array->AnchorCount);
    AtReportAdd (Report, L"  first=%s last=%s", First, Last);
    if (Array->SubsystemToc == 0) {
      AtReportAdd (Report, L"  ToC: NOT FOUND (no back-reference)");
    } else {
      AtReportAdd (Report, L"  ToC=0x%lx encr_required=%u declared=%u",
                   (UINT64)Array->SubsystemToc, Array->EncryptionRequired,
                   Array->TocRegionCount);
      AtReportAdd (Report, L"  count check: walked=%u declared=%u %s",
                   (UINT32)Array->Count, Array->TocRegionCount,
                   (Array->Count == Array->TocRegionCount) ? L"match"
                                                           : L"MISMATCH");
    }
  }

  if (Map->GtocAddress == 0) {
    AtReportAdd (Report, L"G-ToC: not reconstructed (no ToC run)");
  } else {
    AtReportAdd (Report, L"G-ToC @0x%lx subsystems=%u",
                 (UINT64)Map->GtocAddress, (UINT32)Map->SubsystemCount);
    AtReportAdd (Report, L"  status=0x%x revision=0x%x enabled=0x%x",
                 Map->Gtoc.Status, Map->Gtoc.Revision, Map->Gtoc.Enabled);
  }
  AtReportAdd (Report, L"expected total regions=%u (2026-09-02 capture)",
               MD_EXPECT_REGIONS);
  return EFI_SUCCESS;
}

EFI_STATUS
MdBuildRegionReport (
  OUT AT_REPORT *Report
  )
{
  CONST MD_TABLE_MAP *Map;
  MD_REGION_ENTRY    Entry;
  UINTN              ArrayIndex;
  UINTN              EntryIndex;
  UINTN              Total;
  CHAR16             Name[MD_REGION_NAME_LEN + 1];
  EFI_STATUS         Status;

  Map = MdCachedMap ();
  if (Map == NULL) {
    return EFI_NOT_STARTED;
  }
  Status = AtReportInit (Report, MD_REPORT_ROWS);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Total = 0;
  for (ArrayIndex = 0; ArrayIndex < Map->ArrayCount; ArrayIndex++) {
    AtReportAdd (Report, L"[array %u base=0x%lx]", (UINT32)ArrayIndex,
                 (UINT64)Map->Arrays[ArrayIndex].Base);
    for (EntryIndex = 0; EntryIndex < Map->Arrays[ArrayIndex].Count;
         EntryIndex++) {
      Status = MdTableReadEntry (Map, ArrayIndex, EntryIndex, &Entry);
      if (EFI_ERROR (Status)) {
        AtReportAdd (Report, L"  [%u] <read failed: %r>", (UINT32)EntryIndex,
                     Status);
        continue;
      }
      MdAsciiName (Entry.Name, Name, sizeof (Name) / sizeof (CHAR16));
      AtReportAdd (Report, L"  [%u] %s @0x%lx size=%lu",
                   (UINT32)(Total + EntryIndex), Name, Entry.Address,
                   Entry.Size);
    }
    Total += Map->Arrays[ArrayIndex].Count;
  }

  AtReportAdd (Report, L"total regions=%u expected=%u %s", (UINT32)Total,
               MD_EXPECT_REGIONS,
               (Total == MD_EXPECT_REGIONS) ? L"match" : L"MISMATCH");
  AtReportAdd (Report, L"expected UEFI_LOG @0x%lx size=0x%lx",
               MD_EXPECT_UEFI_ADDR, MD_EXPECT_UEFI_SIZE);
  AtReportAdd (Report, L"expected XBL_LOG @0x%lx size=0x%lx",
               MD_EXPECT_XBL_ADDR, MD_EXPECT_XBL_SIZE);
  AtReportAdd (Report, L"expected TZ_DDR @0x%lx size=0x%lx",
               MD_EXPECT_TZ_ADDR, MD_EXPECT_TZ_SIZE);
  return EFI_SUCCESS;
}
