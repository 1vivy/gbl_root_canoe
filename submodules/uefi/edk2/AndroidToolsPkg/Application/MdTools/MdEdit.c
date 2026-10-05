/** @file
  Durable gate, bounded report, and owned-subsystem crash probe.

  Every attempt opens and flushes its evidence file before it asks firmware for
  SMEM item 602. The mutation path then records and flushes the exact 32-byte
  slot store before writing it, verifies the readback, and only then offers the
  deliberate collection trigger.

  Copyright (c) 2026, contributors to the canoe ABL tree.
  SPDX-License-Identifier: BSD-3-Clause
**/

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiLib.h>

#include "MdTools.h"

STATIC UINT8 mProbeBuffer[MD_PROBE_BUFFER_SIZE]
  __attribute__ ((aligned (EFI_PAGE_SIZE)));
STATIC MD_REGION_ENTRY mProbeRegions[1]
  __attribute__ ((aligned (EFI_PAGE_SIZE)));
STATIC BOOLEAN mProbeFilled = FALSE;

STATIC MD_OWNED_REGISTRATION mProbeRegistration;
STATIC UINT64                mTriggerAddress;

EFI_STATUS
MdWriteReports (
  IN OUT AT_EVIDENCE *Evidence
  )
{
  CONST AT_REPORT_SOURCE *Sources;
  AT_REPORT               Report;
  EFI_STATUS              Status;
  UINTN                   Count;
  UINTN                   Index;
  UINTN                   Row;

  if (Evidence == NULL || !Evidence->Open) {
    return EFI_INVALID_PARAMETER;
  }
  Sources = MdReportSources (&Count);
  for (Index = 0; Index < Count; Index++) {
    ZeroMem (&Report, sizeof (Report));
    Status = AtEvidenceWriteAscii (Evidence, "\r\n");
    if (!EFI_ERROR (Status)) {
      Status = AtEvidencePrint (Evidence, L"[%s]", Sources[Index].Title);
    }
    if (!EFI_ERROR (Status)) {
      Status = Sources[Index].Builder (&Report);
    }
    if (EFI_ERROR (Status)) {
      AtReportFree (&Report);
      return Status;
    }
    for (Row = 0; Row < Report.Count && !EFI_ERROR (Status); Row++) {
      Status = AtEvidencePrint (Evidence, L"%s", Report.Rows[Row].Text);
    }
    if (!EFI_ERROR (Status) && Report.Truncated) {
      Status = AtEvidenceWriteAscii (Evidence, "<truncated>\r\n");
    }
    AtReportFree (&Report);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }
  return EFI_SUCCESS;
}


STATIC
EFI_STATUS
MdGateOpen (
  IN  CONST CHAR16 *Step,
  IN  CONST CHAR16 *Tag,
  IN  MD_INTENT_FN  Intent OPTIONAL,
  OUT AT_EVIDENCE  *Evidence
  )
{
  CONST MD_TABLE_MAP *Map;
  EFI_STATUS         Status;
  EFI_STATUS         ScanStatus;
  EFI_STATUS         FlushStatus;

  Status = AtEvidenceOpen (L"md", Tag, 4u, Evidence);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = AtEvidencePrint (
             Evidence,
             L"=== rung %s: %s ===",
             Tag,
             Step
             );
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               Evidence,
               L"stage: discovery not-started; flushing durable marker"
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidenceFlush (Evidence);
  }
  if (EFI_ERROR (Status)) {
    AtEvidenceClose (Evidence);
    return Status;
  }

  ScanStatus = MdEnsureScan (Evidence);
  Map = MdCachedMap ();
  if (EFI_ERROR (ScanStatus)) {
    AtEvidencePrint (
      Evidence,
      L"outcome: discovery REFUSED (%r); nothing written",
      ScanStatus
      );
    AtEvidenceClose (Evidence);
    return ScanStatus;
  }

  Status = AtEvidencePrint (
             Evidence,
             L"discovery=%r arrays=%u source=SMEM item %u",
             ScanStatus,
             (UINT32)((Map != NULL) ? Map->ArrayCount : 0),
             MD_SMEM_ITEM_ID
             );
  if (!EFI_ERROR (Status)) {
    Status = MdWriteReports (Evidence);
  }
  if (!EFI_ERROR (Status) && Intent != NULL) {
    Status = Intent (Evidence);
  }
  if (EFI_ERROR (Status)) {
    AtEvidencePrint (
      Evidence,
      L"intent REFUSED (%r); nothing written",
      Status
      );
    AtEvidenceClose (Evidence);
    return Status;
  }

  FlushStatus = AtEvidenceFlush (Evidence);
  if (EFI_ERROR (FlushStatus)) {
    AtEvidencePrint (
      Evidence,
      L"intent FLUSH FAILED (%r): action skipped",
      FlushStatus
      );
    AtEvidenceClose (Evidence);
    return FlushStatus;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
MdRecord (
  IN CONST CHAR16 *Step,
  IN CONST CHAR16 *Tag,
  IN MD_INTENT_FN  Intent OPTIONAL
  )
{
  AT_EVIDENCE Evidence;
  EFI_STATUS  Status;

  Status = MdGateOpen (Step, Tag, Intent, &Evidence);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = AtEvidencePrint (
             &Evidence,
             L"outcome: report complete, no table write"
             );
  AtEvidenceClose (&Evidence);
  return Status;
}

EFI_STATUS
MdAct (
  IN CONST CHAR16 *Step,
  IN CONST CHAR16 *Tag,
  IN MD_INTENT_FN  Intent OPTIONAL,
  IN MD_RUNG_FN    Act
  )
{
  AT_EVIDENCE Evidence;
  EFI_STATUS  Status;
  EFI_STATUS  FlushStatus;

  if (Act == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = MdGateOpen (Step, Tag, Intent, &Evidence);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = Act (&Evidence);
  if (EFI_ERROR (Status)) {
    AtEvidencePrint (&Evidence, L"outcome: FAILED (%r)", Status);
  } else {
    AtEvidencePrint (&Evidence, L"outcome: rung complete");
  }
  FlushStatus = AtEvidenceFlush (&Evidence);
  if (!EFI_ERROR (Status) && EFI_ERROR (FlushStatus)) {
    Status = FlushStatus;
  }
  AtEvidenceClose (&Evidence);
  return Status;
}

EFI_STATUS
MdWalkPathway (
  IN  CONST MD_PATHWAY *Pathway,
  OUT UINTN            *CompletedRungs OPTIONAL
  )
{
  CONST MD_RUNG *Rung;
  EFI_STATUS     Status;
  UINTN          Index;

  if (Pathway == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (CompletedRungs != NULL) {
    *CompletedRungs = 0;
  }

  for (Index = 0; Index < Pathway->RungCount; ++Index) {
    Rung = &Pathway->Rungs[Index];
    Status = (Rung->Act != NULL)
             ? MdAct (Rung->Name, Rung->Tag, Rung->Intent, Rung->Act)
             : MdRecord (Rung->Name, Rung->Tag, Rung->Intent);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    if (CompletedRungs != NULL) {
      *CompletedRungs = Index + 1;
    }
  }
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
MdMemoryRow (
  IN OUT AT_EVIDENCE *Evidence,
  IN CONST CHAR16    *What,
  IN UINT64           Address
  )
{
  MD_MEMORY_INFO Info;
  EFI_STATUS     Status;

  Status = MdDescribeAddress (Address, &Info);
  if (EFI_ERROR (Status)) {
    return AtEvidencePrint (
             Evidence,
             L"mem %s=0x%lx: NO DESCRIPTOR (%r)",
             What,
             Address,
             Status
             );
  }
  Status = AtEvidencePrint (
             Evidence,
             L"mem %s=0x%lx type=0x%x %s",
             What,
             Address,
             (UINT32)Info.Type,
             MdMemoryTypeName (Info.Type)
             );
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               Evidence,
               L"mem %s: base=0x%lx size=0x%lx attr=0x%lx",
               What,
               Info.Base,
               Info.Size,
               Info.Attributes
               );
  }
  return Status;
}

STATIC
EFI_STATUS
MdTocRows (
  IN OUT AT_EVIDENCE        *Evidence,
  IN CONST MD_SUBSYSTEM_TOC *Toc
  )
{
  EFI_STATUS Status;

  Status = AtEvidencePrint (
             Evidence,
             L"intent: toc init=0x%08x %s enabled=0x%08x %s",
             Toc->Status,
             MdFieldFourcc (Toc->Status),
             Toc->Enabled,
             MdFieldFourcc (Toc->Enabled)
             );
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               Evidence,
               L"intent: toc encr_status=0x%08x %s encr_required=0x%08x %s",
               Toc->EncryptionStatus,
               MdFieldFourcc (Toc->EncryptionStatus),
               Toc->EncryptionRequired,
               MdFieldFourcc (Toc->EncryptionRequired)
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               Evidence,
               L"intent: toc count=%u baseptr=0x%lx",
               Toc->RegionCount,
               Toc->RegionsBasePtr
               );
  }
  return Status;
}

STATIC
VOID
MdProbeInit (
  VOID
  )
{
  if (mProbeFilled) {
    return;
  }

  ZeroMem (mProbeBuffer, sizeof (mProbeBuffer));
  AsciiSPrint (
    (CHAR8 *)mProbeBuffer,
    sizeof (mProbeBuffer),
    "%a %a addr=0x%lx size=%u\r\n",
    MD_PROBE_BANNER,
    MD_PROBE_REGION_NAME,
    (UINT64)(UINTN)mProbeBuffer,
    (UINT32)sizeof (mProbeBuffer)
    );

  ZeroMem (mProbeRegions, sizeof (mProbeRegions));
  CopyMem (
    mProbeRegions[0].Name,
    MD_PROBE_REGION_NAME,
    sizeof (MD_PROBE_REGION_NAME) - 1
    );
  mProbeRegions[0].SeqNum  = 0;
  mProbeRegions[0].Valid   = MD_REGION_VALID_VALUE;
  mProbeRegions[0].Address = (UINT64)(UINTN)mProbeBuffer;
  mProbeRegions[0].Size    = sizeof (mProbeBuffer);

  WriteBackInvalidateDataCacheRange (mProbeBuffer, sizeof (mProbeBuffer));
  WriteBackInvalidateDataCacheRange (mProbeRegions, sizeof (mProbeRegions));
  mProbeFilled = TRUE;
}

EFI_STATUS
MdPrepareOwnedRegistration (
  IN OUT AT_EVIDENCE           *Evidence,
  IN     CONST MD_TABLE_MAP    *Map,
  IN OUT MD_OWNED_REGISTRATION *Registration
  )
{
  CONST MD_REGION_ENTRY *Region;
  UINT32                 Previous[MD_SUBSYSTEM_TOC_SIZE / sizeof (UINT32)];
  EFI_STATUS             Status;

  if (Evidence == NULL || Map == NULL || Registration == NULL ||
      Registration->Regions == NULL || Registration->RegionCount != 1) {
    return EFI_INVALID_PARAMETER;
  }
  Region = &Registration->Regions[0];
  if (Region->Valid != MD_REGION_VALID_VALUE ||
      Region->Address < MD_MIN_REGION_ADDRESS ||
      Region->Address >= MD_MAX_REGION_ADDRESS ||
      Region->Size == 0 || Region->Size > MD_MAX_REGION_SIZE ||
      Region->Name[0] == '\0') {
    return EFI_COMPROMISED_DATA;
  }

  Status = MdTablePlanSubsystemClaim (Map, &Registration->Claim);
  if (EFI_ERROR (Status)) {
    AtEvidencePrint (
      Evidence,
      L"intent REFUSE: no safe free subsystem slot (%r)",
      Status
      );
    return Status;
  }

  CopyMem (Previous, &Registration->Claim.Previous, sizeof (Previous));
  Status = AtEvidencePrint (
             Evidence,
             L"intent: root source=SMEM item %u addr=0x%lx bytes=%u",
             MD_SMEM_ITEM_ID,
             (UINT64)Map->GtocAddress,
             (UINT32)Map->GtocBytes
             );
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               Evidence,
               L"intent: slot=%u of %u at 0x%lx (%s)",
               (UINT32)Registration->Claim.Index,
               MD_MAX_SUBSYSTEMS,
               (UINT64)Registration->Claim.TocAddress,
               Registration->Claim.AboveHighest
               ? L"above highest used" : L"first free fallback"
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               Evidence,
               L"intent: previous32=%08x %08x %08x %08x %08x %08x %08x %08x",
               Previous[0],
               Previous[1],
               Previous[2],
               Previous[3],
               Previous[4],
               Previous[5],
               Previous[6],
               Previous[7]
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               Evidence,
               L"intent: template=AOP slot %u toc=0x%lx",
               MD_SS_AOP,
               (UINT64)Registration->Claim.TemplateToc
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = MdTocRows (Evidence, &Registration->Claim.Template);
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               Evidence,
               L"intent: store 32 bytes after flush: count=%u baseptr=0x%lx",
               Registration->RegionCount,
               (UINT64)(UINTN)Registration->Regions
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               Evidence,
               L"intent: region name=%a addr=0x%lx size=0x%lx",
               Region->Name,
               Region->Address,
               Region->Size
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = MdMemoryRow (
               Evidence,
               L"claim-slot",
               (UINT64)Registration->Claim.TocAddress
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = MdMemoryRow (
               Evidence,
               L"owned-regions",
               (UINT64)(UINTN)Registration->Regions
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = MdMemoryRow (
               Evidence,
               L"registered-payload",
               Region->Address
               );
  }
  return Status;
}

EFI_STATUS
MdApplyOwnedRegistration (
  IN OUT AT_EVIDENCE           *Evidence,
  IN OUT MD_OWNED_REGISTRATION *Registration
  )
{
  MD_TABLE_MAP     *Map;
  MD_SUBSYSTEM_TOC Stored;
  EFI_STATUS       Status;

  if (Evidence == NULL || Registration == NULL ||
      Registration->Regions == NULL || Registration->RegionCount != 1) {
    return EFI_INVALID_PARAMETER;
  }
  Map = (MD_TABLE_MAP *)MdCachedMap ();
  if (Map == NULL) {
    return EFI_NOT_STARTED;
  }

  Status = MdTableClaimSubsystem (
             Map,
             &Registration->Claim,
             (UINT64)(UINTN)Registration->Regions,
             Registration->RegionCount,
             &Stored
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = AtEvidencePrint (
             Evidence,
             L"outcome: slot %u stored init=0x%08x enabled=0x%08x",
             (UINT32)Registration->Claim.Index,
             Stored.Status,
             Stored.Enabled
             );
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               Evidence,
               L"outcome: stored count=%u baseptr=0x%lx encr_required=0x%08x",
               Stored.RegionCount,
               Stored.RegionsBasePtr,
               Stored.EncryptionRequired
               );
  }
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (Stored.RegionCount != Registration->RegionCount ||
      Stored.RegionsBasePtr != (UINT64)(UINTN)Registration->Regions ||
      Stored.Status != Registration->Claim.Template.Status ||
      Stored.Enabled != Registration->Claim.Template.Enabled ||
      Stored.EncryptionStatus != Registration->Claim.Template.EncryptionStatus ||
      Stored.EncryptionRequired != MD_SS_ENCR_NOTREQ_VALUE) {
    return EFI_DEVICE_ERROR;
  }
  return AtEvidencePrint (
           Evidence,
           L"outcome: 32-byte readback verified; owned subsystem is live"
           );
}

STATIC
EFI_STATUS
MdIntentClaimSubsystem (
  IN OUT AT_EVIDENCE *Evidence
  )
{
  CONST MD_TABLE_MAP *Map;

  Map = MdCachedMap ();
  if (Map == NULL) {
    return EFI_NOT_STARTED;
  }

  MdProbeInit ();
  mProbeRegistration.Regions = mProbeRegions;
  mProbeRegistration.RegionCount = 1;
  return MdPrepareOwnedRegistration (Evidence, Map, &mProbeRegistration);
}

STATIC
EFI_STATUS
MdActClaimSubsystem (
  IN OUT AT_EVIDENCE *Evidence
  )
{
  return MdApplyOwnedRegistration (Evidence, &mProbeRegistration);
}

EFI_STATUS
MdIntentCollectionTrigger (
  IN OUT AT_EVIDENCE *Evidence
  )
{
  EFI_STATUS Status;

  if (MD_EXPECT_TZ_SIZE < sizeof (UINT32)) {
    return EFI_COMPROMISED_DATA;
  }
  mTriggerAddress = MD_EXPECT_TZ_ADDR + MD_EXPECT_TZ_SIZE / 2;

  Status = AtEvidencePrint (
             Evidence,
             L"intent: measured TZ_DDR addr=0x%lx size=0x%lx",
             (UINT64)MD_EXPECT_TZ_ADDR,
             (UINT64)MD_EXPECT_TZ_SIZE
             );
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               Evidence,
               L"intent: fire 0x%08x at midpoint 0x%lx",
               (UINT32)MD_CRASH_PATTERN,
               mTriggerAddress
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               Evidence,
               L"intent: no range gate: protected target is deliberate"
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = MdMemoryRow (
               Evidence,
               L"trigger",
               mTriggerAddress
               );
  }
  return Status;
}

EFI_STATUS
MdActCollectionTrigger (
  IN OUT AT_EVIDENCE *Evidence
  )
{
  EFI_STATUS Status;
  UINT32     Readback;

  AtUiBeginScreen (L"Trigger collection", L"Deliberate protected write");
  Print (
    L"write 0x%x at measured TZ_DDR midpoint 0x%lx\r\n",
    (UINT32)MD_CRASH_PATTERN,
    mTriggerAddress
    );
  Print (L"an XPU refusal should enter the 900e dump path\r\n");
  Print (L"the owned subsystem is already registered\r\n");
  AtUiEndScreen (L"Power = fire, Vol +/- = decline");
  if (AtUiWaitForKey (0) != AtKeySelect) {
    AtEvidencePrint (
      Evidence,
      L"outcome: DECLINED; no protected write attempted"
      );
    return EFI_ABORTED;
  }

  AtUiShowMessage (L"Firing trigger...");
  MdTriggerWriteFault (
    mTriggerAddress,
    MD_CRASH_PATTERN,
    &Readback
    );

  Status = AtEvidencePrint (
             Evidence,
             L"outcome: SURVIVED; target writable, no fault occurred"
             );
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               Evidence,
               L"outcome: readback=0x%08x wrote=0x%08x",
               Readback,
               (UINT32)MD_CRASH_PATTERN
               );
  }
  return Status;
}

STATIC CONST MD_RUNG mPathwayReport[] = {
  { L"Bounded map and region report to logfs", L"p1r1", NULL, NULL },
};

STATIC CONST MD_RUNG mPathwayOwnSubsystem[] = {
  { L"Rung 1: bounded map and region report", L"own1", NULL, NULL },
  { L"Rung 2: claim one owned subsystem slot", L"own2",
    MdIntentClaimSubsystem, MdActClaimSubsystem },
  { L"Rung 3: trigger collection (terminal)", L"own3",
    MdIntentCollectionTrigger, MdActCollectionTrigger },
};

STATIC CONST MD_PATHWAY mPathways[] = {
  { L"1 Report (bounded SMEM, no table write)", mPathwayReport,
    sizeof (mPathwayReport) / sizeof (mPathwayReport[0]), TRUE, FALSE },
  { L"2 Own subsystem + collection trigger", mPathwayOwnSubsystem,
    sizeof (mPathwayOwnSubsystem) / sizeof (mPathwayOwnSubsystem[0]),
    FALSE, TRUE },
};

CONST MD_PATHWAY *
MdPathways (
  OUT UINTN *Count
  )
{
  *Count = sizeof (mPathways) / sizeof (mPathways[0]);
  return mPathways;
}
