/** @file
  Reports for the bounded SMEM minidump map.

  Both builders consume the session-cached map. Discovery reads only the
  944-byte global ToC returned as SMEM item 602 and the live AOP and BOOT
  arrays named by that root. The report therefore describes a selected subset,
  not an inferred reconstruction of every subsystem.

  Copyright (c) 2026, contributors to the canoe ABL tree.
  SPDX-License-Identifier: BSD-3-Clause
**/
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>

#include "MdTools.h"

#define MD_REPORT_ROWS  384u

CONST CHAR16 *
MdFieldFourcc (
  IN UINT32 Value
  )
{
  switch (Value) {
  case MD_SS_TOC_MAGIC_VALUE:      return L"(TOC)";
  case MD_SS_AOP_TOC_MAGIC_VALUE:  return L"(AOP)";
  case MD_SS_ENABLED_VALUE:        return L"(ENBL)";
  case MD_SS_DISABLED_VALUE:       return L"(DSBL)";
  case MD_SS_ENCR_DONE_VALUE:      return L"(DONE)";
  case MD_SS_ENCR_REQ_VALUE:       return L"(YES)";
  case MD_SS_ENCR_NOTREQ_VALUE:    return L"(NR)";
  case MD_SS_ENCR_START_VALUE:     return L"(STRT)";
  default:                         return L"(----)";
  }
}


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

/**
  Print the descriptor covering one structure: its type and its attributes,
  both in hex, plus the descriptor's own extent. A missing descriptor is
  itself a finding, so it is printed rather than skipped.
**/
STATIC
VOID
MdAddMemoryRows (
  IN OUT AT_REPORT         *Report,
  IN     CONST CHAR16      *What,
  IN     BOOLEAN           Described,
  IN     CONST MD_MEMORY_INFO *Info
  )
{
  if (!Described) {
    AtReportAdd (Report, L"  %s mem: NO DESCRIPTOR CONTAINS THIS ADDRESS",
                 What);
    return;
  }
  AtReportAdd (Report, L"  %s mem: type=0x%x %s attr=0x%lx", What,
               (UINT32)Info->Type, MdMemoryTypeName (Info->Type),
               Info->Attributes);
  AtReportAdd (Report, L"  %s mem: base=0x%lx size=0x%lx", What, Info->Base,
               Info->Size);
}

/** Print the six named words of a subsystem ToC as raw values. **/
STATIC
VOID
MdAddTocRows (
  IN OUT AT_REPORT          *Report,
  IN     CONST MD_SUBSYSTEM_TOC *Toc
  )
{
  AtReportAdd (Report, L"  toc0 init/status    =0x%08x %s", Toc->Status,
               MdFieldFourcc (Toc->Status));
  AtReportAdd (Report, L"  toc1 enabled        =0x%08x %s", Toc->Enabled,
               MdFieldFourcc (Toc->Enabled));
  AtReportAdd (Report, L"  toc2 encryption_stat=0x%08x %s",
               Toc->EncryptionStatus, MdFieldFourcc (Toc->EncryptionStatus));
  AtReportAdd (Report, L"  toc3 encr_required  =0x%08x %s",
               Toc->EncryptionRequired,
               MdFieldFourcc (Toc->EncryptionRequired));
  AtReportAdd (Report, L"  toc4 region_count   =0x%08x (%u)", Toc->RegionCount,
               Toc->RegionCount);
  AtReportAdd (Report, L"  toc5 pad            =0x%08x %s", Toc->Pad,
               (Toc->Pad == 0) ? L"(zero)" : L"(NONZERO)");
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

  AtReportAdd (
    Report,
    L"discovery: SMEM item %u (no descriptor scan)",
    MD_SMEM_ITEM_ID
    );
  AtReportAdd (
    Report,
    L"G-ToC @0x%lx bytes=%u required=%u",
    (UINT64)Map->GtocAddress,
    (UINT32)Map->GtocBytes,
    (UINT32)sizeof (MD_GLOBAL_TOC)
    );
  AtReportAdd (
    Report,
    L"  source=%s header=%s structure=%s",
    Map->GtocFromSmem ? L"SMEM" : L"NOT SMEM",
    Map->GtocHeaderValid ? L"VALID" : L"NOT VALID",
    Map->GtocStructContiguous ? L"one descriptor" : L"NOT one descriptor"
    );
  AtReportAdd (
    Report,
    L"  status=0x%x revision=0x%x enabled=0x%x pad=0x%x",
    Map->Gtoc.Status,
    Map->Gtoc.Revision,
    Map->Gtoc.Enabled,
    Map->Gtoc.Pad
    );
  AtReportAdd (
    Report,
    L"  subsystems=%u expected=%u",
    (UINT32)Map->SubsystemCount,
    MD_MAX_SUBSYSTEMS
    );
  MdAddMemoryRows (Report, L"gtoc", Map->GtocDescribed, &Map->GtocMemory);
  AtReportAdd (
    Report,
    L"selected arrays=%u (only AOP slot %u and BOOT slot %u)",
    (UINT32)Map->ArrayCount,
    MD_SS_AOP,
    MD_SS_BOOT
    );
  AtReportAdd (
    Report,
    L"magics: TOC init, ENBL enabled, DONE encr done, YES req, NR notreq"
    );

  for (Index = 0; Index < Map->ArrayCount; ++Index) {
    CONST MD_REGION_ARRAY *Array;

    Array = &Map->Arrays[Index];
    MdAsciiName (Array->FirstName, First, sizeof (First) / sizeof (CHAR16));
    MdAsciiName (Array->LastName, Last, sizeof (Last) / sizeof (CHAR16));
    AtReportAdd (
      Report,
      L"array %u: subsystem=%u base=0x%lx count=%u",
      (UINT32)Index,
      (UINT32)Array->SubsystemIndex,
      (UINT64)Array->Base,
      (UINT32)Array->Count
      );
    AtReportAdd (Report, L"  first=%s last=%s", First, Last);
    MdAddMemoryRows (Report, L"base", Array->BaseDescribed, &Array->BaseMemory);
    AtReportAdd (
      Report,
      L"  ToC=0x%lx encr_required=0x%08x declared=%u",
      (UINT64)Array->SubsystemToc,
      Array->EncryptionRequired,
      Array->TocRegionCount
      );
    AtReportAdd (
      Report,
      L"  count check: mapped=%u declared=%u %s",
      (UINT32)Array->Count,
      Array->TocRegionCount,
      (Array->Count == Array->TocRegionCount) ? L"match" : L"MISMATCH"
      );
    MdAddTocRows (Report, &Array->Toc);
    AtReportAdd (
      Report,
      L"  toc6 regions_baseptr=0x%lx %s",
      Array->TocBasePtr,
      (Array->TocBasePtr == (UINT64)Array->Base)
      ? L"(matches mapped base)" : L"MISMATCH"
      );
    MdAddMemoryRows (Report, L"toc", Array->TocDescribed, &Array->TocMemory);
  }

  AtReportAdd (
    Report,
    L"capture reference: %u total regions across all subsystems",
    MD_EXPECT_REGIONS
    );
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
    AtReportAdd (
      Report,
      L"[subsystem %u array=0x%lx]",
      (UINT32)Map->Arrays[ArrayIndex].SubsystemIndex,
      (UINT64)Map->Arrays[ArrayIndex].Base
      );
    for (EntryIndex = 0; EntryIndex < Map->Arrays[ArrayIndex].Count;
         EntryIndex++) {
      Status = MdTableReadEntry (Map, ArrayIndex, EntryIndex, &Entry);
      if (EFI_ERROR (Status)) {
        AtReportAdd (
          Report,
          L"  [%u] <read failed: %r>",
          (UINT32)EntryIndex,
          Status
          );
        continue;
      }
      MdAsciiName (Entry.Name, Name, sizeof (Name) / sizeof (CHAR16));
      AtReportAdd (
        Report,
        L"  [%u] %s @0x%lx size=%lu",
        (UINT32)EntryIndex,
        Name,
        Entry.Address,
        Entry.Size
        );
    }
    Total += Map->Arrays[ArrayIndex].Count;
  }

  AtReportAdd (
    Report,
    L"selected regions=%u (AOP + BOOT only; full capture reference=%u)",
    (UINT32)Total,
    MD_EXPECT_REGIONS
    );
  AtReportAdd (
    Report,
    L"expected UEFI_LOG @0x%lx size=0x%lx",
    MD_EXPECT_UEFI_ADDR,
    MD_EXPECT_UEFI_SIZE
    );
  AtReportAdd (
    Report,
    L"expected XBL_LOG @0x%lx size=0x%lx",
    MD_EXPECT_XBL_ADDR,
    MD_EXPECT_XBL_SIZE
    );
  AtReportAdd (
    Report,
    L"measured trigger TZ_DDR @0x%lx size=0x%lx",
    MD_EXPECT_TZ_ADDR,
    MD_EXPECT_TZ_SIZE
    );
  return EFI_SUCCESS;
}
