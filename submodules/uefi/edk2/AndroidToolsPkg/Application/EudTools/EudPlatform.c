/** @file
 *  EUD secure-call, MMIO, COM, durable-evidence, and minidump telemetry flows.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/AndroidToolsEvidence.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/IoLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/scm_sip_interface.h>

#include "EudTools.h"

#define EUD_FLAG_SCM_PRESENT       (1u << 0)
#define EUD_FLAG_MD_ARMED          (1u << 1)
#define EUD_FLAG_SECURE_ATTEMPTED  (1u << 2)
#define EUD_FLAG_SECURE_ACCEPTED   (1u << 3)
#define EUD_FLAG_NONSECURE_ENABLED (1u << 4)
#define EUD_FLAG_ATTACH_SET        (1u << 5)
#define EUD_FLAG_COM_TRAFFIC       (1u << 6)

#define EUD_STATUS_REPORT_ROWS  48u

typedef struct {
  BOOLEAN                Initialized;
  BOOLEAN                BaselineValid;
  BOOLEAN                EnabledByTool;
  BOOLEAN                UnsafeToExit;
  BOOLEAN                MinidumpArmed;
  BOOLEAN                ModeBeforeValid;
  UINT32                 ModeBefore;
  AT_SOC_INFO            Soc;
  CONST EUD_SOC_PROFILE *Profile;
  QCOM_SCM_PROTOCOL      *Scm;
  EFI_STATUS             ScmLocateStatus;
  EUD_REGISTER_SNAPSHOT  Baseline;
  MD_TABLE_MAP           MdMap;
  MD_SUBSYSTEM_CLAIM     MdClaim;
  MD_SUBSYSTEM_TOC       MdStored;
} EUD_SESSION;

STATIC EUD_SESSION mSession;
STATIC EUD_TELEMETRY_PAGE mTelemetry
  __attribute__ ((aligned (EFI_PAGE_SIZE)));
STATIC MD_REGION_ENTRY mTelemetryRegion[1]
  __attribute__ ((aligned (EFI_PAGE_SIZE)));

STATIC UINTN
EudAddress (
  IN UINT32 Offset
  )
{
  return (UINTN)(mSession.Profile->RegisterBase + Offset);
}

STATIC BOOLEAN
EudProfileSupported (
  VOID
  )
{
  return (BOOLEAN)(mSession.Profile != NULL);
}

STATIC VOID
EudTelemetryCommit (
  IN EUD_STAGE  Stage,
  IN EFI_STATUS Status
  )
{
  mTelemetry.Data.Magic = EUD_TELEMETRY_MAGIC;
  mTelemetry.Data.Version = EUD_TELEMETRY_VERSION;
  mTelemetry.Data.Bytes = sizeof (mTelemetry.Data);
  mTelemetry.Data.Stage = (UINT32)Stage;
  mTelemetry.Data.Sequence++;
  mTelemetry.Data.LastStatus = (UINT64)Status;
  WriteBackInvalidateDataCacheRange (&mTelemetry, sizeof (mTelemetry));
}

STATIC VOID
EudSnapshotRegisters (
  OUT EUD_REGISTER_SNAPSHOT *Snapshot
  )
{
  ZeroMem (Snapshot, sizeof (*Snapshot));
  Snapshot->TxId = MmioRead32 (EudAddress (EUD_REG_COM_TX_ID));
  Snapshot->TxLength = MmioRead32 (EudAddress (EUD_REG_COM_TX_LEN));
  Snapshot->RxId = MmioRead32 (EudAddress (EUD_REG_COM_RX_ID));
  Snapshot->RxLength = MmioRead32 (EudAddress (EUD_REG_COM_RX_LEN));
  Snapshot->InterruptMask = MmioRead32 (EudAddress (EUD_REG_INT1_ENABLE_MASK));
  Snapshot->InterruptStatus = MmioRead32 (EudAddress (EUD_REG_INT_STATUS_1));
  Snapshot->ControlOut = MmioRead32 (EudAddress (EUD_REG_CONTROL_OUT_1));
  Snapshot->CsrEnable = MmioRead32 (EudAddress (EUD_REG_CSR_ENABLE));
  Snapshot->AttachDetect = MmioRead32 (EudAddress (EUD_REG_SW_ATTACH_DETECT));
  Snapshot->UtmiDelayLow = MmioRead16 (EudAddress (EUD_REG_UTMI_DELAY_LOW));
  Snapshot->UtmiDelayHigh = MmioRead16 (EudAddress (EUD_REG_UTMI_DELAY_HIGH));
}

STATIC EFI_STATUS
EudScmIoRead (
  OUT EUD_SCM_RESULT *Result
  )
{
  UINT64 Parameters[SCM_MAX_NUM_PARAMETERS];

  if (Result == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (Result, sizeof (*Result));
  if (!EudProfileSupported ()) {
    Result->TransportStatus = EFI_UNSUPPORTED;
    return Result->TransportStatus;
  }
  if (mSession.Scm == NULL || mSession.Scm->ScmSipSysCall == NULL) {
    Result->TransportStatus = EFI_NOT_FOUND;
    return Result->TransportStatus;
  }
  ZeroMem (Parameters, sizeof (Parameters));
  Parameters[0] = mSession.Profile->ModeManagerAddress;
  Result->TransportStatus = mSession.Scm->ScmSipSysCall (
    mSession.Scm,
    TZ_IO_ACCESS_READ_ID,
    TZ_IO_ACCESS_READ_ID_PARAM_ID,
    Parameters,
    Result->Results
    );
  if (!EFI_ERROR (Result->TransportStatus) && Result->Results[0] == 1u) {
    Result->Value = (UINT32)Result->Results[1];
    Result->ValueValid = TRUE;
  }
  return Result->TransportStatus;
}

STATIC EFI_STATUS
EudScmIoWrite (
  IN  UINT32          Value,
  OUT EUD_SCM_RESULT *Result
  )
{
  UINT64 Parameters[SCM_MAX_NUM_PARAMETERS];

  if (Result == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (Result, sizeof (*Result));
  if (!EudProfileSupported ()) {
    Result->TransportStatus = EFI_UNSUPPORTED;
    return Result->TransportStatus;
  }
  if (mSession.Scm == NULL || mSession.Scm->ScmSipSysCall == NULL) {
    Result->TransportStatus = EFI_NOT_FOUND;
    return Result->TransportStatus;
  }
  ZeroMem (Parameters, sizeof (Parameters));
  Parameters[0] = mSession.Profile->ModeManagerAddress;
  Parameters[1] = Value;
  Result->TransportStatus = mSession.Scm->ScmSipSysCall (
    mSession.Scm,
    TZ_IO_ACCESS_WRITE_ID,
    TZ_IO_ACCESS_WRITE_ID_PARAM_ID,
    Parameters,
    Result->Results
    );
  return Result->TransportStatus;
}

STATIC VOID
EudRememberScmResult (
  IN CONST EUD_SCM_RESULT *Result
  )
{
  if (Result == NULL) {
    return;
  }
  CopyMem (mTelemetry.Data.ScmResults, Result->Results,
           sizeof (mTelemetry.Data.ScmResults));
  mTelemetry.Data.LastStatus = (UINT64)Result->TransportStatus;
}

STATIC EFI_STATUS
EudLogScmResult (
  IN OUT AT_EVIDENCE        *Evidence,
  IN     CONST CHAR16       *Name,
  IN     CONST EUD_SCM_RESULT *Result
  )
{
  EFI_STATUS Status;

  Status = AtEvidencePrint (
             Evidence,
             L"%s.transport_status=0x%016lx",
             Name,
             (UINT64)Result->TransportStatus
             );
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               Evidence,
               L"%s.results=%016lx %016lx %016lx %016lx",
               Name,
               Result->Results[0], Result->Results[1],
               Result->Results[2], Result->Results[3]
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               Evidence,
               L"%s.value=%s0x%08x",
               Name,
               Result->ValueValid ? L"" : L"unknown/",
               Result->Value
               );
  }
  return Status;
}

STATIC EFI_STATUS
EudEvidenceOpen (
  IN  CONST CHAR16 *Tag,
  IN  CONST CHAR16 *Title,
  OUT AT_EVIDENCE  *Evidence
  )
{
  EFI_STATUS Status;

  Status = AtEvidenceOpen (L"eud", Tag, EUD_EVIDENCE_KEEP, Evidence);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  EudTelemetryCommit (EudStageEvidenceOpen, EFI_SUCCESS);
  Status = AtEvidencePrint (Evidence, L"=== EudTools: %s ===", Title);
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               Evidence,
               L"schema=2 soc=%s raw_id=0x%08x chip=%a detect=0x%016lx",
               AtSocKindName (mSession.Soc.Kind),
               mSession.Soc.RawChipId,
               mSession.Soc.ChipIdString,
               (UINT64)mSession.Soc.RawIdStatus
               );
  }
  if (!EFI_ERROR (Status) && EudProfileSupported ()) {
    Status = AtEvidencePrint (
               Evidence,
               L"profile=%s eud_base=0x%lx mode_manager=0x%lx com_id=0x%02x",
               mSession.Profile->Name,
               mSession.Profile->RegisterBase,
               mSession.Profile->ModeManagerAddress,
               mSession.Profile->ComExecutionId
               );
  }
  if (!EFI_ERROR (Status) && !EudProfileSupported ()) {
    Status = AtEvidencePrint (
               Evidence,
               L"profile=unsupported; no EUD MMIO or secure IO will run"
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               Evidence,
               L"scm.locate_status=0x%016lx scm.revision=0x%016lx",
               (UINT64)mSession.ScmLocateStatus,
               (mSession.Scm != NULL) ? mSession.Scm->Revision : 0
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               Evidence,
               L"telemetry.addr=0x%lx bytes=%u",
               (UINT64)(UINTN)&mTelemetry,
               (UINT32)sizeof (mTelemetry)
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidenceFlush (Evidence);
  }
  if (EFI_ERROR (Status)) {
    AtEvidenceClose (Evidence);
  }
  return Status;
}

STATIC EFI_STATUS
EudIntent (
  IN OUT AT_EVIDENCE *Evidence,
  IN     EUD_STAGE    Stage,
  IN     CONST CHAR16 *Text
  )
{
  EFI_STATUS Status;

  EudTelemetryCommit (Stage, EFI_NOT_READY);
  Status = AtEvidencePrint (Evidence, L"stage.%s=pending %s",
                            EudStageName (Stage), Text);
  if (!EFI_ERROR (Status)) {
    Status = AtEvidenceFlush (Evidence);
  }
  return Status;
}

STATIC EFI_STATUS
EudArmMinidump (
  IN OUT AT_EVIDENCE *Evidence
  )
{
  EFI_PHYSICAL_ADDRESS Root;
  EFI_STATUS           Status;
  UINTN                Bytes;

  if (mSession.MinidumpArmed) {
    return EFI_ALREADY_STARTED;
  }
  if (!EudProfileSupported () ||
      !mSession.Profile->MinidumpTelemetrySupported) {
    Status = AtEvidencePrint (
               Evidence,
               L"minidump.state=unsupported-for-soc soc=%s",
               AtSocKindName (mSession.Soc.Kind)
               );
    if (!EFI_ERROR (Status)) {
      Status = AtEvidenceFlush (Evidence);
    }
    return EFI_ERROR (Status) ? Status : EFI_UNSUPPORTED;
  }
  Status = EudIntent (Evidence, EudStageMinidumpDiscover,
                      L"resolve bounded SMEM item 602");
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Root = 0;
  Bytes = 0;
  Status = MdTableLocateRoot (&Root, &Bytes);
  if (!EFI_ERROR (Status)) {
    Status = MdTableScanRoot (Root, Bytes, &mSession.MdMap);
  }
  if (!EFI_ERROR (Status)) {
    Status = MdTablePlanSubsystemClaim (&mSession.MdMap, &mSession.MdClaim);
  }
  if (EFI_ERROR (Status)) {
    AtEvidencePrint (Evidence, L"minidump.state=unavailable status=%r", Status);
    AtEvidenceFlush (Evidence);
    EudTelemetryCommit (EudStageMinidumpDiscover, Status);
    return Status;
  }

  ZeroMem (mTelemetryRegion, sizeof (mTelemetryRegion));
  CopyMem (mTelemetryRegion[0].Name, EUD_MD_REGION_NAME,
           sizeof (EUD_MD_REGION_NAME) - 1);
  mTelemetryRegion[0].Valid = MD_REGION_VALID_VALUE;
  mTelemetryRegion[0].Address = (UINT64)(UINTN)&mTelemetry;
  mTelemetryRegion[0].Size = sizeof (mTelemetry);
  if (!MdRangeWritable ((UINT64)(UINTN)mTelemetryRegion,
                        sizeof (mTelemetryRegion)) ||
      !MdRangeWritable ((UINT64)(UINTN)&mTelemetry, sizeof (mTelemetry))) {
    Status = EFI_ACCESS_DENIED;
    AtEvidencePrint (
      Evidence,
      L"minidump.state=unavailable telemetry or region array is not writable"
      );
    AtEvidenceFlush (Evidence);
    EudTelemetryCommit (EudStageMinidumpDiscover, Status);
    return Status;
  }
  WriteBackInvalidateDataCacheRange (mTelemetryRegion,
                                     sizeof (mTelemetryRegion));

  Status = EudIntent (Evidence, EudStageMinidumpClaim,
                      L"claim one free subsystem slot for SM8850-EUD");
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               Evidence,
               L"minidump.intent slot=%u toc=0x%lx regions=0x%lx payload=0x%lx",
               (UINT32)mSession.MdClaim.Index,
               (UINT64)mSession.MdClaim.TocAddress,
               (UINT64)(UINTN)mTelemetryRegion,
               (UINT64)(UINTN)&mTelemetry
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidenceFlush (Evidence);
  }
  if (!EFI_ERROR (Status)) {
    Status = MdTableClaimSubsystem (
               &mSession.MdMap,
               &mSession.MdClaim,
               (UINT64)(UINTN)mTelemetryRegion,
               1,
               &mSession.MdStored
               );
  }
  if (EFI_ERROR (Status)) {
    AtEvidencePrint (Evidence, L"minidump.state=claim-failed status=%r", Status);
    AtEvidenceFlush (Evidence);
    EudTelemetryCommit (EudStageMinidumpClaim, Status);
    return Status;
  }
  mSession.MinidumpArmed = TRUE;
  mTelemetry.Data.Flags |= EUD_FLAG_MD_ARMED;
  EudTelemetryCommit (EudStageMinidumpClaim, EFI_SUCCESS);
  Status = AtEvidencePrint (
             Evidence,
             L"minidump.state=armed slot=%u name=%a",
             (UINT32)mSession.MdClaim.Index,
             EUD_MD_REGION_NAME
             );
  if (!EFI_ERROR (Status)) {
    Status = AtEvidenceFlush (Evidence);
  }
  return Status;
}

STATIC EFI_STATUS
EudDisarmMinidump (
  IN OUT AT_EVIDENCE *Evidence
  )
{
  EFI_STATUS Status;

  if (!mSession.MinidumpArmed) {
    return EFI_SUCCESS;
  }
  Status = EudIntent (Evidence, EudStageMinidumpRelease,
                      L"restore the exact previously-empty subsystem slot");
  if (!EFI_ERROR (Status)) {
    Status = MdTableReleaseSubsystemClaim (
               &mSession.MdMap,
               &mSession.MdClaim,
               &mSession.MdStored
               );
  }
  if (EFI_ERROR (Status)) {
    mSession.UnsafeToExit = TRUE;
    AtEvidencePrint (
      Evidence,
      L"minidump.state=release-failed status=%r; tool must remain resident",
      Status
      );
    AtEvidenceFlush (Evidence);
    EudTelemetryCommit (EudStageMinidumpRelease, Status);
    return Status;
  }
  mSession.MinidumpArmed = FALSE;
  mTelemetry.Data.Flags &= ~EUD_FLAG_MD_ARMED;
  ZeroMem (&mSession.MdMap, sizeof (mSession.MdMap));
  ZeroMem (&mSession.MdClaim, sizeof (mSession.MdClaim));
  ZeroMem (&mSession.MdStored, sizeof (mSession.MdStored));
  EudTelemetryCommit (EudStageMinidumpRelease, EFI_SUCCESS);
  Status = AtEvidencePrint (Evidence, L"minidump.state=released");
  if (!EFI_ERROR (Status)) {
    Status = AtEvidenceFlush (Evidence);
  }
  return Status;
}

STATIC EFI_STATUS
EudFinishEvidence (
  IN OUT AT_EVIDENCE *Evidence,
  IN     EFI_STATUS   OperationStatus
  )
{
  EFI_STATUS CloseStatus;
  EFI_STATUS MdStatus;

  MdStatus = EudDisarmMinidump (Evidence);
  AtEvidencePrint (Evidence, L"operation.status=%r", OperationStatus);
  AtEvidencePrint (Evidence, L"minidump.release_status=%r", MdStatus);
  EudTelemetryCommit (EudStageComplete, OperationStatus);
  CloseStatus = AtEvidenceClose (Evidence);
  if (EFI_ERROR (OperationStatus)) {
    return OperationStatus;
  }
  if (EFI_ERROR (MdStatus)) {
    return MdStatus;
  }
  return CloseStatus;
}

EFI_STATUS
EudSessionInitialize (
  VOID
  )
{
  if (mSession.Initialized) {
    return EFI_SUCCESS;
  }
  ZeroMem (&mSession, sizeof (mSession));
  ZeroMem (&mTelemetry, sizeof (mTelemetry));
  (VOID)AtSocDetect (&mSession.Soc);
  mSession.Profile = EudProfileForSocKind (mSession.Soc.Kind);
  mSession.ScmLocateStatus = gBS->LocateProtocol (
    &gQcomScmProtocolGuid,
    NULL,
    (VOID **)&mSession.Scm
    );
  if (!EFI_ERROR (mSession.ScmLocateStatus) && mSession.Scm != NULL &&
      mSession.Scm->ScmSipSysCall != NULL) {
    mTelemetry.Data.Flags |= EUD_FLAG_SCM_PRESENT;
  } else {
    mSession.Scm = NULL;
  }
  mSession.Initialized = TRUE;
  EudTelemetryCommit (EudStageIdle, EFI_SUCCESS);
  return EFI_SUCCESS;
}

STATIC CONST CHAR16 *
EudSecureOutcomeName (
  IN EUD_SECURE_OUTCOME Outcome
  )
{
  switch (Outcome) {
  case EudSecureNotRun:          return L"not-run";
  case EudSecureAccepted:        return L"accepted";
  case EudSecureRejected:        return L"rejected";
  case EudSecureTransportError:  return L"transport-error";
  default:                       return L"unknown";
  }
}

EFI_STATUS
EudBuildStatusReport (
  OUT AT_REPORT *Report
  )
{
  EUD_REGISTER_SNAPSHOT Snapshot;
  EUD_SCM_RESULT         Secure;
  EUD_SECURE_OUTCOME     Outcome;
  EFI_STATUS             Status;

  if (Report == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Status = AtReportInit (Report, EUD_STATUS_REPORT_ROWS);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  AtReportAdd (Report, L"soc.kind=%s", AtSocKindName (mSession.Soc.Kind));
  AtReportAdd (Report, L"soc.raw_id=0x%08x chip=%a",
               mSession.Soc.RawChipId, mSession.Soc.ChipIdString);
  AtReportAdd (Report, L"soc.locate=0x%016lx raw=0x%016lx name=0x%016lx",
               (UINT64)mSession.Soc.LocateStatus,
               (UINT64)mSession.Soc.RawIdStatus,
               (UINT64)mSession.Soc.NameStatus);
  if (!EudProfileSupported ()) {
    AtReportAdd (Report, L"profile=unsupported; MMIO and secure IO refused");
    return EFI_SUCCESS;
  }
  EudSnapshotRegisters (&Snapshot);
  EudScmIoRead (&Secure);
  Outcome = EudClassifySecureResult (TRUE, &Secure);
  AtReportAdd (Report, L"profile=%s", mSession.Profile->Name);
  AtReportAdd (Report, L"eud.base=0x%lx", mSession.Profile->RegisterBase);
  AtReportAdd (Report, L"eud.mode_manager=0x%lx",
               mSession.Profile->ModeManagerAddress);
  AtReportAdd (Report, L"scm.locate_status=0x%016lx",
               (UINT64)mSession.ScmLocateStatus);
  AtReportAdd (Report, L"scm.revision=0x%016lx",
               (mSession.Scm != NULL) ? mSession.Scm->Revision : 0);
  AtReportAdd (Report, L"secure.read.transport=0x%016lx",
               (UINT64)Secure.TransportStatus);
  AtReportAdd (Report, L"secure.read.results=%016lx %016lx %016lx %016lx",
               Secure.Results[0], Secure.Results[1],
               Secure.Results[2], Secure.Results[3]);
  AtReportAdd (Report, L"secure.read.outcome=%s",
               EudSecureOutcomeName (Outcome));
  AtReportAdd (Report, L"secure.mode.value=%s0x%08x",
               Secure.ValueValid ? L"" : L"unknown/", Secure.Value);
  AtReportAdd (Report, L"csr.enable=0x%08x", Snapshot.CsrEnable);
  AtReportAdd (Report, L"sw.attach=0x%08x", Snapshot.AttachDetect);
  AtReportAdd (Report, L"interrupt.mask=0x%08x", Snapshot.InterruptMask);
  AtReportAdd (Report, L"interrupt.status=0x%08x", Snapshot.InterruptStatus);
  AtReportAdd (Report, L"control.out=0x%08x", Snapshot.ControlOut);
  AtReportAdd (Report, L"com.tx.id=0x%08x len=%u",
               Snapshot.TxId, Snapshot.TxLength);
  AtReportAdd (Report, L"com.rx.id=0x%08x len=%u",
               Snapshot.RxId, Snapshot.RxLength);
  AtReportAdd (Report, L"utmi.delay.low=0x%04x high=0x%04x",
               Snapshot.UtmiDelayLow, Snapshot.UtmiDelayHigh);
  AtReportAdd (
    Report,
    L"minidump.telemetry=%s",
    mSession.Profile->MinidumpTelemetrySupported ? L"qualified" : L"disabled"
    );
  AtReportAdd (Report, L"session.baseline=%s enabled_by_tool=%s",
               mSession.BaselineValid ? L"saved" : L"none",
               mSession.EnabledByTool ? L"true" : L"false");
  AtReportAdd (Report, L"minidump.armed=%s safe_to_exit=%s",
               mSession.MinidumpArmed ? L"true" : L"false",
               mSession.UnsafeToExit ? L"false" : L"true");
  return EFI_SUCCESS;
}

EFI_STATUS
EudRecordStatus (
  VOID
  )
{
  AT_EVIDENCE Evidence;
  AT_REPORT   Report;
  EFI_STATUS Status;
  UINTN      Index;

  ZeroMem (&Report, sizeof (Report));
  Status = EudEvidenceOpen (L"status", L"passive register and SCM-read report",
                            &Evidence);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = EudBuildStatusReport (&Report);
  for (Index = 0; !EFI_ERROR (Status) && Index < Report.Count; Index++) {
    Status = AtEvidencePrint (&Evidence, L"%s", Report.Rows[Index].Text);
  }
  AtReportFree (&Report);
  if (!EFI_ERROR (Status)) {
    Status = AtEvidenceFlush (&Evidence);
  }
  return EudFinishEvidence (&Evidence, Status);
}

EFI_STATUS
EudProbeSecureGate (
  VOID
  )
{
  AT_EVIDENCE       Evidence;
  EUD_SCM_RESULT    Before;
  EUD_SCM_RESULT    Enable;
  EUD_SCM_RESULT    After;
  EUD_SCM_RESULT    Restore;
  EUD_SCM_RESULT    Verify;
  EFI_STATUS        Status;
  UINT32            RestoreValue;

  Status = EudEvidenceOpen (L"scm", L"secure EUD mode-manager write probe",
                            &Evidence);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (!EudProfileSupported ()) {
    return EudFinishEvidence (&Evidence, EFI_UNSUPPORTED);
  }
  EudArmMinidump (&Evidence);

  Status = EudIntent (&Evidence, EudStageSecureReadBefore,
                      L"TZ_IO_ACCESS_READ profile mode-manager");
  if (!EFI_ERROR (Status)) {
    EudScmIoRead (&Before);
    EudRememberScmResult (&Before);
    Status = EudLogScmResult (&Evidence, L"secure.before", &Before);
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidenceFlush (&Evidence);
  }
  if (EFI_ERROR (Status)) {
    return EudFinishEvidence (&Evidence, Status);
  }
  if (!Before.ValueValid) {
    AtEvidencePrint (
      &Evidence,
      L"secure.enable=refused; original mode bit could not be read for restore"
      );
    AtEvidenceFlush (&Evidence);
    return EudFinishEvidence (&Evidence, EFI_NOT_READY);
  }


  Status = EudIntent (&Evidence, EudStageSecureWriteEnable,
                      L"TZ_IO_ACCESS_WRITE profile mode-manager <- 1");
  if (!EFI_ERROR (Status)) {
    mTelemetry.Data.Flags |= EUD_FLAG_SECURE_ATTEMPTED;
    EudScmIoWrite (1, &Enable);
    EudRememberScmResult (&Enable);
    if (EudClassifySecureResult (TRUE, &Enable) == EudSecureAccepted) {
      mTelemetry.Data.Flags |= EUD_FLAG_SECURE_ACCEPTED;
    }
    Status = EudLogScmResult (&Evidence, L"secure.enable", &Enable);
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidenceFlush (&Evidence);
  }
  if (EFI_ERROR (Status)) {
    return EudFinishEvidence (&Evidence, Status);
  }

  Status = EudIntent (&Evidence, EudStageSecureReadAfter,
                      L"read back profile mode-manager after enable attempt");
  if (!EFI_ERROR (Status)) {
    EudScmIoRead (&After);
    EudRememberScmResult (&After);
    Status = EudLogScmResult (&Evidence, L"secure.after", &After);
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidenceFlush (&Evidence);
  }

  if (!EFI_ERROR (Status) && (Before.Value & 1u) == 0) {
    RestoreValue = 0;
    Status = EudIntent (&Evidence, EudStageSecureRestore,
                        L"restore the mode-manager bit observed before probe");
    if (!EFI_ERROR (Status)) {
      EudScmIoWrite (RestoreValue, &Restore);
      EudRememberScmResult (&Restore);
      Status = EudLogScmResult (&Evidence, L"secure.restore", &Restore);
    }
    if (!EFI_ERROR (Status)) {
      EudScmIoRead (&Verify);
      EudRememberScmResult (&Verify);
      Status = EudLogScmResult (
                 &Evidence,
                 L"secure.restore_readback",
                 &Verify
                 );
    }
    if (!EFI_ERROR (Status) &&
        (!Verify.ValueValid || (Verify.Value & 1u) != RestoreValue)) {
      Status = EFI_DEVICE_ERROR;
    }
    if (!EFI_ERROR (Status)) {
      Status = AtEvidenceFlush (&Evidence);
    }
  }
  return EudFinishEvidence (&Evidence, Status);
}

STATIC EFI_STATUS
EudPetAttach (
  VOID
  )
{
  UINT32 Current;
  UINTN  Attempt;

  Current = MmioRead32 (EudAddress (EUD_REG_SW_ATTACH_DETECT));
  if ((Current & 1u) != 0) {
    MmioWrite32 (EudAddress (EUD_REG_SW_ATTACH_DETECT), 0);
    for (Attempt = 0; Attempt < 100; Attempt++) {
      if ((MmioRead32 (EudAddress (EUD_REG_SW_ATTACH_DETECT)) & 1u) == 0) {
        break;
      }
      gBS->Stall (1);
    }
    if (Attempt == 100) {
      return EFI_TIMEOUT;
    }
  }
  MmioWrite32 (EudAddress (EUD_REG_SW_ATTACH_DETECT), 1);
  return ((MmioRead32 (EudAddress (EUD_REG_SW_ATTACH_DETECT)) & 1u) != 0)
         ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS
EudEnablePath (
  IN BOOLEAN AttemptSecureWrite
  )
{
  AT_EVIDENCE          Evidence;
  EUD_SCM_RESULT       SecureBefore;
  EUD_SCM_RESULT       SecureEnable;
  EUD_SCM_RESULT       SecureAfter;
  EFI_STATUS           Status;

  Status = EudEvidenceOpen (
             AttemptSecureWrite ? L"enable" : L"reject",
             AttemptSecureWrite
             ? L"secure plus nonsecure EUD enable path"
             : L"nonsecure rejection-path EUD enable",
             &Evidence
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (!EudProfileSupported ()) {
    return EudFinishEvidence (&Evidence, EFI_UNSUPPORTED);
  }
  EudArmMinidump (&Evidence);
  if (!mSession.BaselineValid) {
    EudSnapshotRegisters (&mSession.Baseline);
    CopyMem (&mTelemetry.Data.Before, &mSession.Baseline,
             sizeof (mSession.Baseline));
    mSession.BaselineValid = TRUE;
  }

  if (AttemptSecureWrite) {
    Status = EudIntent (&Evidence, EudStageSecureReadBefore,
                        L"capture mode-manager value before write");
    if (!EFI_ERROR (Status)) {
      EudScmIoRead (&SecureBefore);
      EudRememberScmResult (&SecureBefore);
      if (!mSession.ModeBeforeValid && SecureBefore.ValueValid) {
        mSession.ModeBeforeValid = TRUE;
        mSession.ModeBefore = SecureBefore.Value;
        mTelemetry.Data.ModeBefore = SecureBefore.Value;
      }
      Status = EudLogScmResult (&Evidence, L"secure.before", &SecureBefore);
    }
    if (!EFI_ERROR (Status)) {
      Status = AtEvidenceFlush (&Evidence);
    }
    if (EFI_ERROR (Status)) {
      return EudFinishEvidence (&Evidence, Status);
    }

    Status = EudIntent (&Evidence, EudStageSecureWriteEnable,
                        L"attempt secure mode-manager enable; continue on rejection");
    if (!EFI_ERROR (Status)) {
      mTelemetry.Data.Flags |= EUD_FLAG_SECURE_ATTEMPTED;
      EudScmIoWrite (1, &SecureEnable);
      EudRememberScmResult (&SecureEnable);
      if (EudClassifySecureResult (TRUE, &SecureEnable) == EudSecureAccepted) {
        mTelemetry.Data.Flags |= EUD_FLAG_SECURE_ACCEPTED;
      }
      Status = EudLogScmResult (&Evidence, L"secure.enable", &SecureEnable);
    }
    if (!EFI_ERROR (Status)) {
      Status = AtEvidenceFlush (&Evidence);
    }
    if (EFI_ERROR (Status)) {
      return EudFinishEvidence (&Evidence, Status);
    }

    Status = EudIntent (&Evidence, EudStageSecureReadAfter,
                        L"read mode-manager value after secure enable attempt");
    if (!EFI_ERROR (Status)) {
      EudScmIoRead (&SecureAfter);
      EudRememberScmResult (&SecureAfter);
      mTelemetry.Data.ModeAfter = SecureAfter.Value;
      Status = EudLogScmResult (&Evidence, L"secure.after", &SecureAfter);
    }
    if (!EFI_ERROR (Status)) {
      Status = AtEvidenceFlush (&Evidence);
    }
    if (EFI_ERROR (Status)) {
      return EudFinishEvidence (&Evidence, Status);
    }
  } else {
    Status = AtEvidencePrint (
               &Evidence,
               L"secure.enable=skipped; explicit nonsecure rejection-path run"
               );
    if (!EFI_ERROR (Status)) {
      Status = AtEvidenceFlush (&Evidence);
    }
  }

  if (!EFI_ERROR (Status)) {
    Status = EudIntent (&Evidence, EudStageUtmiProgram,
                        L"program profile UTMI delay");
  }
  if (!EFI_ERROR (Status)) {
    MmioWrite16 (
      EudAddress (EUD_REG_UTMI_DELAY_HIGH),
      mSession.Profile->UtmiDelayHigh
      );
    MmioWrite16 (
      EudAddress (EUD_REG_UTMI_DELAY_LOW),
      mSession.Profile->UtmiDelayLow
      );
    Status = AtEvidencePrint (
               &Evidence,
               L"utmi.readback low=0x%04x high=0x%04x",
               MmioRead16 (EudAddress (EUD_REG_UTMI_DELAY_LOW)),
               MmioRead16 (EudAddress (EUD_REG_UTMI_DELAY_HIGH))
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidenceFlush (&Evidence);
  }
  if (EFI_ERROR (Status)) {
    return EudFinishEvidence (&Evidence, Status);
  }

  Status = EudIntent (&Evidence, EudStageCsrEnable,
                      L"write CSR enable and VBUS/charger/safe-mode mask");
  if (!EFI_ERROR (Status)) {
    MmioWrite32 (EudAddress (EUD_REG_CSR_ENABLE), 1);
    MmioWrite32 (EudAddress (EUD_REG_INT1_ENABLE_MASK),
                 EUD_ENABLE_INTERRUPT_MASK);
    Status = AtEvidencePrint (
               &Evidence,
               L"csr.readback=0x%08x interrupt.mask=0x%08x",
               MmioRead32 (EudAddress (EUD_REG_CSR_ENABLE)),
               MmioRead32 (EudAddress (EUD_REG_INT1_ENABLE_MASK))
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidenceFlush (&Evidence);
  }
  if (EFI_ERROR (Status)) {
    return EudFinishEvidence (&Evidence, Status);
  }

  Status = EudIntent (&Evidence, EudStageAttach,
                      L"detach/pet/attach the EUD mini-hub");
  if (!EFI_ERROR (Status)) {
    Status = EudPetAttach ();
  }
  if (!EFI_ERROR (Status)) {
    EudSnapshotRegisters (&mTelemetry.Data.After);
    mTelemetry.Data.Flags |= EUD_FLAG_NONSECURE_ENABLED |
                             EUD_FLAG_ATTACH_SET;
    mSession.EnabledByTool = TRUE;
    Status = AtEvidencePrint (
               &Evidence,
               L"attach.readback=0x%08x control.out=0x%08x interrupt.status=0x%08x",
               mTelemetry.Data.After.AttachDetect,
               mTelemetry.Data.After.ControlOut,
               mTelemetry.Data.After.InterruptStatus
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               &Evidence,
               L"host.enumeration=unobserved; inspect host USB descriptors"
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidenceFlush (&Evidence);
  }
  return EudFinishEvidence (&Evidence, Status);
}

EFI_STATUS
EudRestoreBaseline (
  VOID
  )
{
  AT_EVIDENCE       Evidence;
  EUD_SCM_RESULT    Restore;
  EUD_SCM_RESULT    Verify;
  EUD_REGISTER_SNAPSHOT After;
  EFI_STATUS        Status;

  if (!EudProfileSupported ()) {
    return EFI_UNSUPPORTED;
  }

  if (!mSession.BaselineValid) {
    return EFI_NOT_STARTED;
  }
  Status = EudEvidenceOpen (L"restore", L"restore pre-tool EUD state",
                            &Evidence);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  EudArmMinidump (&Evidence);
  Status = EudIntent (&Evidence, EudStageRestoreNonsecure,
                      L"restore attach, mask, CSR and UTMI snapshots");
  if (!EFI_ERROR (Status)) {
    MmioWrite32 (EudAddress (EUD_REG_SW_ATTACH_DETECT),
                 mSession.Baseline.AttachDetect);
    MmioWrite32 (EudAddress (EUD_REG_INT1_ENABLE_MASK),
                 mSession.Baseline.InterruptMask);
    MmioWrite32 (EudAddress (EUD_REG_CSR_ENABLE),
                 mSession.Baseline.CsrEnable);
    MmioWrite16 (EudAddress (EUD_REG_UTMI_DELAY_LOW),
                 mSession.Baseline.UtmiDelayLow);
    MmioWrite16 (EudAddress (EUD_REG_UTMI_DELAY_HIGH),
                 mSession.Baseline.UtmiDelayHigh);
    EudSnapshotRegisters (&After);
    Status = AtEvidencePrint (
               &Evidence,
               L"restore.readback csr=0x%08x attach=0x%08x mask=0x%08x utmi=%04x/%04x",
               After.CsrEnable, After.AttachDetect, After.InterruptMask,
               After.UtmiDelayLow, After.UtmiDelayHigh
               );
    if (!EFI_ERROR (Status) &&
        (After.CsrEnable != mSession.Baseline.CsrEnable ||
         After.AttachDetect != mSession.Baseline.AttachDetect ||
         After.InterruptMask != mSession.Baseline.InterruptMask ||
         After.UtmiDelayLow != mSession.Baseline.UtmiDelayLow ||
         After.UtmiDelayHigh != mSession.Baseline.UtmiDelayHigh)) {
      Status = EFI_DEVICE_ERROR;
    }
  }
  if (!EFI_ERROR (Status) && mSession.ModeBeforeValid) {
    Status = EudIntent (&Evidence, EudStageSecureRestore,
                        L"restore secure mode-manager bit observed before enable");
    if (!EFI_ERROR (Status)) {
      EudScmIoWrite (mSession.ModeBefore & 1u, &Restore);
      EudRememberScmResult (&Restore);
      Status = EudLogScmResult (&Evidence, L"secure.restore", &Restore);
    }
    if (!EFI_ERROR (Status)) {
      EudScmIoRead (&Verify);
      EudRememberScmResult (&Verify);
      Status = EudLogScmResult (&Evidence, L"secure.restore_readback", &Verify);
    }
    if (!EFI_ERROR (Status) &&
        (!Verify.ValueValid ||
         (Verify.Value & 1u) != (mSession.ModeBefore & 1u))) {
      Status = EFI_DEVICE_ERROR;
    }
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidenceFlush (&Evidence);
  }
  if (!EFI_ERROR (Status)) {
    mSession.EnabledByTool = FALSE;
    mSession.BaselineValid = FALSE;
    mSession.ModeBeforeValid = FALSE;
    mTelemetry.Data.Flags &= ~(EUD_FLAG_NONSECURE_ENABLED |
                               EUD_FLAG_ATTACH_SET);
  }
  return EudFinishEvidence (&Evidence, Status);
}

STATIC VOID
EudComWriteFrame (
  IN CONST UINT8 *Data,
  IN UINT32       Length
  )
{
  UINT32 Index;

  MmioWrite32 (
    EudAddress (EUD_REG_COM_TX_ID),
    mSession.Profile->ComExecutionId
    );
  MmioWrite32 (EudAddress (EUD_REG_COM_TX_LEN), Length);
  for (Index = 0; Index < Length; Index++) {
    MmioWrite32 (EudAddress (EUD_REG_COM_TX_DATA), Data[Index]);
  }
}

STATIC BOOLEAN
EudComReadFrame (
  OUT UINT32 *Id,
  OUT UINT8  *Data,
  OUT UINT32 *Length
  )
{
  UINT32 Index;

  *Id = MmioRead32 (EudAddress (EUD_REG_COM_RX_ID));
  *Length = MmioRead32 (EudAddress (EUD_REG_COM_RX_LEN));
  if (!EudComFrameValid (mSession.Profile->ComExecutionId, *Id, *Length)) {
    return FALSE;
  }
  for (Index = 0; Index < *Length; Index++) {
    Data[Index] = (UINT8)MmioRead32 (EudAddress (EUD_REG_COM_RX_DATA));
  }
  return TRUE;
}

EFI_STATUS
EudRunComTest (
  VOID
  )
{
  STATIC CONST UINT8 Banner[] = "SOC-EUD OK\r\n";
  AT_EVIDENCE Evidence;
  EFI_STATUS  Status;
  UINT32      Id;
  UINT32      Index;
  UINT32      Length;
  UINT32      OriginalMask;
  UINT32      InterruptStatus;
  BOOLEAN     BannerSent;

  if (!EudProfileSupported ()) {
    return EFI_UNSUPPORTED;
  }

  if ((MmioRead32 (EudAddress (EUD_REG_CSR_ENABLE)) & 1u) == 0) {
    return EFI_NOT_STARTED;
  }
  Status = EudEvidenceOpen (L"com", L"bounded bidirectional EUD COM test",
                            &Evidence);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  EudArmMinidump (&Evidence);
  OriginalMask = MmioRead32 (EudAddress (EUD_REG_INT1_ENABLE_MASK));
  Status = EudIntent (&Evidence, EudStageComTest,
                      L"enable RX/TX indications and poll for ten seconds");
  if (EFI_ERROR (Status)) {
    return EudFinishEvidence (&Evidence, Status);
  }
  MmioWrite32 (EudAddress (EUD_REG_INT1_ENABLE_MASK),
               OriginalMask | EUD_INT_RX | EUD_INT_TX);
  BannerSent = FALSE;
  for (Index = 0; Index < EUD_COM_TEST_MS; Index++) {
    InterruptStatus = MmioRead32 (EudAddress (EUD_REG_INT_STATUS_1));
    if (!BannerSent && (InterruptStatus & EUD_INT_TX) != 0) {
      EudComWriteFrame (Banner, sizeof (Banner) - 1);
      mTelemetry.Data.ComTxFrames++;
      BannerSent = TRUE;
      mTelemetry.Data.Flags |= EUD_FLAG_COM_TRAFFIC;
    }
    if ((InterruptStatus & EUD_INT_RX) != 0) {
      ZeroMem (mTelemetry.Data.LastRx, sizeof (mTelemetry.Data.LastRx));
      if (EudComReadFrame (&Id, mTelemetry.Data.LastRx, &Length)) {
        mTelemetry.Data.ComRxFrames++;
        mTelemetry.Data.LastRxId = Id;
        mTelemetry.Data.LastRxLength = Length;
        mTelemetry.Data.Flags |= EUD_FLAG_COM_TRAFFIC;
      } else {
        mTelemetry.Data.ComInvalidFrames++;
        mTelemetry.Data.LastRxId = Id;
        mTelemetry.Data.LastRxLength = Length;
      }
    }
    if ((Index % 100u) == 0) {
      EudTelemetryCommit (EudStageComTest, EFI_NOT_READY);
    }
    gBS->Stall (1000);
  }
  MmioWrite32 (EudAddress (EUD_REG_INT1_ENABLE_MASK), OriginalMask);
  Status = AtEvidencePrint (
             &Evidence,
             L"com.outcome tx=%lu rx=%lu invalid=%lu banner=%s",
             mTelemetry.Data.ComTxFrames,
             mTelemetry.Data.ComRxFrames,
             mTelemetry.Data.ComInvalidFrames,
             BannerSent ? L"sent" : L"host-never-requested-tx"
             );
  if (!EFI_ERROR (Status)) {
    Status = AtEvidencePrint (
               &Evidence,
               L"com.last_rx id=0x%08x len=%u data=%02x%02x%02x%02x%02x%02x%02x%02x",
               mTelemetry.Data.LastRxId,
               mTelemetry.Data.LastRxLength,
               mTelemetry.Data.LastRx[0], mTelemetry.Data.LastRx[1],
               mTelemetry.Data.LastRx[2], mTelemetry.Data.LastRx[3],
               mTelemetry.Data.LastRx[4], mTelemetry.Data.LastRx[5],
               mTelemetry.Data.LastRx[6], mTelemetry.Data.LastRx[7]
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = AtEvidenceFlush (&Evidence);
  }
  return EudFinishEvidence (&Evidence, Status);
}

BOOLEAN
EudCanExit (
  VOID
  )
{
  return (BOOLEAN)(!mSession.MinidumpArmed && !mSession.UnsafeToExit);
}
