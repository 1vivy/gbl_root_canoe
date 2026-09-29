/** @file
  Interactive one-region shadow registration for bounded AOP/BOOT targets.

  The selected payload is not copied. MdTools registers one resident region
  entry with a unique Canoe alias and the AOP not-encryption-required template,
  then triggers collection before its image can exit.

  Copyright (c) 2026, contributors to the canoe ABL tree.
  SPDX-License-Identifier: BSD-3-Clause
**/
#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiLib.h>

#include "MdTools.h"
#include "MdShadow.h"

STATIC MD_SHADOW_TARGET mTargets[MD_SHADOW_MAX_TARGETS];
STATIC CHAR16 mLabels[MD_SHADOW_MAX_TARGETS + 1][MD_SHADOW_LABEL_CHARS];
STATIC CONST CHAR16 *mItems[MD_SHADOW_MAX_TARGETS + 1];

STATIC MD_SHADOW_TARGET mSelectedTarget;
STATIC CHAR8 mSelectedSourceName[MD_REGION_NAME_LEN + 1];
STATIC MD_REGION_ENTRY mShadowRegions[1]
  __attribute__ ((aligned (EFI_PAGE_SIZE)));
STATIC MD_OWNED_REGISTRATION mShadowRegistration;
STATIC BOOLEAN mSelectionValid = FALSE;
STATIC BOOLEAN mShadowAttempted = FALSE;

STATIC
CONST CHAR16 *
MdShadowSubsystemName (
  IN UINTN SubsystemIndex
  )
{
  return (SubsystemIndex == MD_SS_AOP) ? L"AOP" : L"BOOT";
}

STATIC
CONST CHAR16 *
MdShadowEncryptionName (
  IN UINT32 EncryptionRequired
  )
{
  if (EncryptionRequired == MD_SS_ENCR_NOTREQ_VALUE) {
    return L"not-required";
  }
  if (EncryptionRequired == MD_SS_ENCR_REQ_VALUE) {
    return L"required";
  }
  return L"unknown";
}

STATIC
VOID
MdShadowCopySourceName (
  OUT CHAR8                 Name[MD_REGION_NAME_LEN + 1],
  IN  CONST MD_REGION_ENTRY *Entry
  )
{
  ZeroMem (Name, MD_REGION_NAME_LEN + 1);
  CopyMem (Name, Entry->Name, MD_REGION_NAME_LEN);
}

STATIC
VOID
MdShadowBuildLabel (
  IN  CONST MD_SHADOW_TARGET *Target,
  OUT CHAR16                 *Label
  )
{
  CHAR16 Name[MD_REGION_NAME_LEN + 1];
  UINTN  Index;

  ZeroMem (Name, sizeof (Name));
  for (Index = 0;
       Index < MD_REGION_NAME_LEN && Target->Source.Name[Index] != '\0';
       ++Index) {
    Name[Index] = (CHAR16)(UINT8)Target->Source.Name[Index];
  }
  UnicodeSPrint (
    Label,
    MD_SHADOW_LABEL_CHARS * sizeof (CHAR16),
    L"%s[%u] %s size=0x%lx enc=%s -> %a",
    MdShadowSubsystemName (Target->SubsystemIndex),
    (UINT32)Target->EntryIndex,
    Name,
    Target->Source.Size,
    MdShadowEncryptionName (Target->EncryptionRequired),
    Target->Alias
    );
}

STATIC
EFI_STATUS
MdShadowSelect (
  IN CONST MD_SHADOW_TARGET *Target
  )
{
  EFI_STATUS Status;

  Status = MdShadowBuildRegion (Target, &mShadowRegions[0]);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  CopyMem (&mSelectedTarget, Target, sizeof (mSelectedTarget));
  MdShadowCopySourceName (mSelectedSourceName, &Target->Source);
  WriteBackInvalidateDataCacheRange (mShadowRegions, sizeof (mShadowRegions));

  ZeroMem (&mShadowRegistration, sizeof (mShadowRegistration));
  mShadowRegistration.Regions = mShadowRegions;
  mShadowRegistration.RegionCount = 1;
  mSelectionValid = TRUE;
  return EFI_SUCCESS;
}

STATIC
VOID
MdShadowIntro (
  VOID
  )
{
  Print (
    L"source: %s[%u] %a\r\n",
    MdShadowSubsystemName (mSelectedTarget.SubsystemIndex),
    (UINT32)mSelectedTarget.EntryIndex,
    mSelectedSourceName
    );
  Print (
    L"payload: 0x%lx bytes at 0x%lx; source enc=%s\r\n",
    mSelectedTarget.Source.Size,
    mSelectedTarget.Source.Address,
    MdShadowEncryptionName (mSelectedTarget.EncryptionRequired)
    );
  Print (L"shadow alias: %a; encryption not required\r\n",
         mSelectedTarget.Alias);
  Print (L"RAM-only: this does NOT carry into Android userspace.\r\n");
}

STATIC
EFI_STATUS
MdIntentShadowClaim (
  IN OUT MD_EVIDENCE *Evidence
  )
{
  CONST MD_TABLE_MAP *Map;
  MD_REGION_ENTRY     Current;
  EFI_STATUS          Status;

  if (!mSelectionValid || mShadowAttempted) {
    return EFI_NOT_READY;
  }
  Map = MdCachedMap ();
  if (Map == NULL) {
    return EFI_NOT_STARTED;
  }

  Status = MdTableReadEntry (
             Map,
             mSelectedTarget.ArrayIndex,
             mSelectedTarget.EntryIndex,
             &Current
             );
  if (EFI_ERROR (Status) ||
      CompareMem (&Current, &mSelectedTarget.Source, sizeof (Current)) != 0) {
    MdEvidencePrint (
      Evidence,
      L"intent REFUSE: selected source descriptor changed (%r)",
      EFI_ERROR (Status) ? Status : EFI_ABORTED
      );
    return EFI_ERROR (Status) ? Status : EFI_ABORTED;
  }

  Status = MdEvidencePrint (
             Evidence,
             L"intent: shadow source=%s array=%u entry=%u name=%a",
             MdShadowSubsystemName (mSelectedTarget.SubsystemIndex),
             (UINT32)mSelectedTarget.ArrayIndex,
             (UINT32)mSelectedTarget.EntryIndex,
             mSelectedSourceName
             );
  if (!EFI_ERROR (Status)) {
    Status = MdEvidencePrint (
               Evidence,
               L"intent: selected source descriptor readback verified"
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = MdEvidencePrint (
               Evidence,
               L"intent: source addr=0x%lx size=0x%lx encr_required=0x%08x %s",
               mSelectedTarget.Source.Address,
               mSelectedTarget.Source.Size,
               mSelectedTarget.EncryptionRequired,
               MdFieldFourcc (mSelectedTarget.EncryptionRequired)
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = MdEvidencePrint (
               Evidence,
               L"intent: alias=%a seq=0; payload is not copied",
               mSelectedTarget.Alias
               );
  }
  if (EFI_ERROR (Status)) {
    return Status;
  }
  return MdPrepareOwnedRegistration (
           Evidence,
           Map,
           &mShadowRegistration
           );
}

STATIC
EFI_STATUS
MdActShadowClaim (
  IN OUT MD_EVIDENCE *Evidence
  )
{
  EFI_STATUS Status;

  /* Once this act starts, a failure may still follow a completed slot store.
     Refuse another selection until reset rather than repoint this array under a
     possibly live earlier slot. */
  mShadowAttempted = TRUE;
  Status = MdApplyOwnedRegistration (Evidence, &mShadowRegistration);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  return MdEvidencePrint (
           Evidence,
           L"outcome: shadow %a aliases %s[%u] %a",
           mSelectedTarget.Alias,
           MdShadowSubsystemName (mSelectedTarget.SubsystemIndex),
           (UINT32)mSelectedTarget.EntryIndex,
           mSelectedSourceName
           );
}

STATIC CONST MD_RUNG mShadowRungs[] = {
  { L"Rung 1: bounded map and region report", L"shd1", NULL, NULL },
  { L"Rung 2: register selected region under plaintext alias", L"shd2",
    MdIntentShadowClaim, MdActShadowClaim },
  { L"Rung 3: trigger collection (terminal)", L"shd3",
    MdIntentCollectionTrigger, MdActCollectionTrigger },
};

STATIC CONST MD_PATHWAY mShadowPathway = {
  L"Shadow selected region + collection trigger",
  mShadowRungs,
  sizeof (mShadowRungs) / sizeof (mShadowRungs[0]),
  FALSE,
  TRUE
};

VOID
MdRunShadowTargetMenu (
  VOID
  )
{
  CONST MD_TABLE_MAP *Map;
  EFI_STATUS         Status;
  UINTN              Count;
  UINTN              Index;
  UINTN              Selected;

  if (mShadowAttempted) {
    AtUiReportStatus (
      L"Shadow registration already attempted; reset before another",
      EFI_ALREADY_STARTED
      );
    return;
  }

  AtUiShowMessage (L"Recording and enumerating bounded targets...");
  Status = MdRecord (L"Enumerate shadow targets", L"shpk", NULL);
  if (EFI_ERROR (Status)) {
    AtUiReportStatus (L"Shadow target discovery", Status);
    return;
  }
  Map = MdCachedMap ();
  if (Map == NULL) {
    AtUiReportStatus (L"Shadow target discovery", EFI_NOT_STARTED);
    return;
  }

  Status = MdShadowCollectTargets (
             Map,
             mTargets,
             MD_SHADOW_MAX_TARGETS,
             &Count
             );
  if (EFI_ERROR (Status)) {
    AtUiReportStatus (L"Shadow target enumeration", Status);
    return;
  }
  if (Count == 0) {
    AtUiReportStatus (L"No validated AOP/BOOT regions", EFI_NOT_FOUND);
    return;
  }

  for (Index = 0; Index < Count; ++Index) {
    MdShadowBuildLabel (&mTargets[Index], mLabels[Index]);
    mItems[Index] = mLabels[Index];
  }
  mItems[Count] = L"Back";

  Status = AtUiRunMenu (
             L"Shadow minidump target",
             mItems,
             Count + 1,
             &Selected,
             L"One target per reset; target array remains in MdTools RAM"
             );
  if (EFI_ERROR (Status) || Selected >= Count) {
    return;
  }

  Status = MdShadowSelect (&mTargets[Selected]);
  if (EFI_ERROR (Status)) {
    AtUiReportStatus (L"Shadow target preparation", Status);
    return;
  }
  MdRunPathwayScreen (&mShadowPathway, MdShadowIntro);
}
