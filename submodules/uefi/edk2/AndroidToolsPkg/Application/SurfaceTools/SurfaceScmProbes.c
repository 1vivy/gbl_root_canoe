/** @file
 * Fixed-allowlist SCM readback and complete parseable report rows.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "SurfacePolicy.h"
#include "SurfaceScmProbes.h"

#define ST_POLICY_CANARY_BYTES  64u


STATIC_ASSERT (
  OFFSET_OF (QCOM_SCM_PROTOCOL, ScmSipSysCall) == 56,
  "SCM common-prefix ABI changed"
  );
STATIC_ASSERT (
  SCM_MAX_NUM_PARAMETERS >= 2 && SCM_MAX_NUM_RESULTS >= 3,
  "SCM argument arrays are too small"
  );
STATIC CONST UINT8 mCanaryByte = 0xa5;

STATIC CONST CHAR16 *
BoolName (IN BOOLEAN Value)
{
  return Value ? L"true" : L"false";
}

STATIC CONST CHAR16 *
CallState (
  IN EFI_STATUS Status,
  IN BOOLEAN    Complete
  )
{
  if (Status == EFI_ACCESS_DENIED || Status == EFI_SECURITY_VIOLATION) {
    return L"denied";
  }
  if (Status == EFI_UNSUPPORTED) {
    return L"unsupported";
  }
  if (Status != EFI_SUCCESS || !Complete) {
    return L"error";
  }
  return L"authorized";
}

STATIC VOID
AddUnknownPolicyRows (
  IN OUT AT_REPORT    *Report,
  IN     CONST CHAR16 *Valid
  )
{
  AtReportAdd (Report, L"policy.valid=%s", Valid);
  AtReportAdd (Report, L"policy.layout=unknown");
  AtReportAdd (Report, L"policy.flags0=unknown");
  AtReportAdd (Report, L"policy.flags1=unknown");
  AtReportAdd (Report, L"policy.online=unknown");
  AtReportAdd (Report, L"policy.offline=unknown");
  AtReportAdd (Report, L"policy.jtag=unknown");
  AtReportAdd (Report, L"policy.logs=unknown");
  AtReportAdd (Report, L"policy.modem_invasive=unknown");
  AtReportAdd (Report, L"policy.modem_noninvasive=unknown");
  AtReportAdd (Report, L"policy.apps_invasive=unknown");
  AtReportAdd (Report, L"policy.debug_level=unknown");
  AtReportAdd (Report, L"policy.nonsecure_dump=unknown");
  AtReportAdd (Report, L"policy.encrypted_apps=unknown");
  AtReportAdd (Report, L"policy.encrypted_mpss=unknown");
  AtReportAdd (Report, L"policy.encrypted_lpass=unknown");
  AtReportAdd (Report, L"policy.encrypted_css=unknown");
  AtReportAdd (Report, L"policy.encrypted_adsp=unknown");
  AtReportAdd (Report, L"policy.encrypted_cdsp=unknown");
  AtReportAdd (Report, L"policy.flag31=unknown");
  AtReportAdd (Report, L"policy.flag31_meaning=unknown");
  AtReportAdd (Report, L"policy.image_id_bitmap=unknown");
  AtReportAdd (Report, L"policy.root_count=unknown");
  AtReportAdd (Report, L"policy.serial_count=unknown");
  AtReportAdd (Report, L"policy.qc_root_count=unknown");
  AtReportAdd (Report, L"policy.oem_flags=unknown");
}

STATIC VOID
AddPolicyRows (
  IN OUT AT_REPORT           *Report,
  IN     CONST ST_POLICY_VIEW *View
  )
{
  UINT32 DebugLevel;
  UINT64 Flags;

  Flags = View->Flags;
  DebugLevel = (UINT32)((Flags >> ST_POLICY_FLAG_DEBUG_LEVEL0) & 3U);
  AtReportAdd (Report, L"policy.valid=true");
  AtReportAdd (Report, L"policy.layout=%s",
               (View->Layout == StPolicyLayoutRevision2) ?
               L"revision2" : L"revision5");
  AtReportAdd (Report, L"policy.flags0=0x%08x", (UINT32)Flags);
  AtReportAdd (Report, L"policy.flags1=0x%08x", (UINT32)(Flags >> 32));
  AtReportAdd (Report, L"policy.online=%s",
               BoolName ((BOOLEAN)((Flags & (1ULL << ST_POLICY_FLAG_ONLINE)) != 0)));
  AtReportAdd (Report, L"policy.offline=%s",
               BoolName ((BOOLEAN)((Flags & (1ULL << ST_POLICY_FLAG_OFFLINE)) != 0)));
  AtReportAdd (Report, L"policy.jtag=%s",
               BoolName ((BOOLEAN)((Flags & (1ULL << ST_POLICY_FLAG_JTAG)) != 0)));
  AtReportAdd (Report, L"policy.logs=%s",
               BoolName ((BOOLEAN)((Flags & (1ULL << ST_POLICY_FLAG_LOGS)) != 0)));
  if (View->Layout == StPolicyLayoutRevision5) {
    AtReportAdd (Report, L"policy.modem_invasive=%s",
                 BoolName ((BOOLEAN)((Flags & (1ULL << ST_POLICY_FLAG_MODEM_INV)) != 0)));
    AtReportAdd (Report, L"policy.modem_noninvasive=%s",
                 BoolName ((BOOLEAN)((Flags & (1ULL << ST_POLICY_FLAG_MODEM_NINV)) != 0)));
    AtReportAdd (Report, L"policy.apps_invasive=%s",
                 BoolName ((BOOLEAN)((Flags & (1ULL << ST_POLICY_FLAG_APPS_INV)) != 0)));
    AtReportAdd (Report, L"policy.debug_level=%u", DebugLevel);
    AtReportAdd (Report, L"policy.nonsecure_dump=%s",
                 BoolName ((BOOLEAN)((Flags & (1ULL << ST_POLICY_FLAG_NONSECURE_DUMP)) != 0)));
    AtReportAdd (Report, L"policy.encrypted_apps=%s",
                 BoolName ((BOOLEAN)((Flags & (1ULL << ST_POLICY_FLAG_ENCRYPTED_APPS)) != 0)));
    AtReportAdd (Report, L"policy.encrypted_mpss=%s",
                 BoolName ((BOOLEAN)((Flags & (1ULL << ST_POLICY_FLAG_ENCRYPTED_MPSS)) != 0)));
    AtReportAdd (Report, L"policy.encrypted_lpass=%s",
                 BoolName ((BOOLEAN)((Flags & (1ULL << ST_POLICY_FLAG_ENCRYPTED_LPASS)) != 0)));
    AtReportAdd (Report, L"policy.encrypted_css=%s",
                 BoolName ((BOOLEAN)((Flags & (1ULL << ST_POLICY_FLAG_ENCRYPTED_CSS)) != 0)));
    AtReportAdd (Report, L"policy.encrypted_adsp=%s",
                 BoolName ((BOOLEAN)((Flags & (1ULL << ST_POLICY_FLAG_ENCRYPTED_ADSP)) != 0)));
    AtReportAdd (Report, L"policy.encrypted_cdsp=%s",
                 BoolName ((BOOLEAN)((Flags & (1ULL << ST_POLICY_FLAG_ENCRYPTED_CDSP)) != 0)));
    AtReportAdd (Report, L"policy.flag31=%u",
                 (UINT32)((Flags >> ST_POLICY_FLAG_SCHEMA_CONFLICT) & 1U));
    AtReportAdd (Report, L"policy.flag31_meaning=unknown");
  } else {
    AtReportAdd (Report, L"policy.modem_invasive=not_applicable");
    AtReportAdd (Report, L"policy.modem_noninvasive=not_applicable");
    AtReportAdd (Report, L"policy.apps_invasive=not_applicable");
    AtReportAdd (Report, L"policy.debug_level=not_applicable");
    AtReportAdd (Report, L"policy.nonsecure_dump=not_applicable");
    AtReportAdd (Report, L"policy.encrypted_apps=not_applicable");
    AtReportAdd (Report, L"policy.encrypted_mpss=not_applicable");
    AtReportAdd (Report, L"policy.encrypted_lpass=not_applicable");
    AtReportAdd (Report, L"policy.encrypted_css=not_applicable");
    AtReportAdd (Report, L"policy.encrypted_adsp=not_applicable");
    AtReportAdd (Report, L"policy.encrypted_cdsp=not_applicable");
    AtReportAdd (Report, L"policy.flag31=not_applicable");
    AtReportAdd (Report, L"policy.flag31_meaning=not_applicable");
  }
  AtReportAdd (Report, L"policy.image_id_bitmap=0x%08x", View->ImageIdBitmap);
  AtReportAdd (Report, L"policy.root_count=%u", View->RootCount);
  AtReportAdd (Report, L"policy.serial_count=%u", View->SerialCount);
  if (View->Layout == StPolicyLayoutRevision5) {
    AtReportAdd (Report, L"policy.qc_root_count=%u", View->QcRootCount);
  } else {
    AtReportAdd (Report, L"policy.qc_root_count=not_applicable");
  }
  AtReportAdd (Report, L"policy.oem_flags=0x%04x", View->OemFlags);
}

#define ST_POLICY_HEX_CHUNK_BYTES  32u

STATIC VOID
AddPolicyPayloadRows (
  IN OUT AT_REPORT    *Report,
  IN     CONST UINT8  *Payload,
  IN     UINTN         PayloadSize
  )
{
  CHAR16 Hex[ST_POLICY_HEX_CHUNK_BYTES * 2 + 1];
  UINTN ChunkCount;
  UINTN ChunkSize;
  UINTN Offset;

  ChunkCount = (PayloadSize + ST_POLICY_HEX_CHUNK_BYTES - 1) /
               ST_POLICY_HEX_CHUNK_BYTES;
  AtReportAdd (Report, L"policy.raw_encoding=hex");
  AtReportAdd (Report, L"policy.raw_size=%u", (UINT32)PayloadSize);
  AtReportAdd (Report, L"policy.raw_chunk_bytes=%u",
               ST_POLICY_HEX_CHUNK_BYTES);
  AtReportAdd (Report, L"policy.raw_chunks=%u", (UINT32)ChunkCount);

  for (Offset = 0; Offset < PayloadSize;
       Offset += ST_POLICY_HEX_CHUNK_BYTES) {
    ChunkSize = PayloadSize - Offset;
    if (ChunkSize > ST_POLICY_HEX_CHUNK_BYTES) {
      ChunkSize = ST_POLICY_HEX_CHUNK_BYTES;
    }
    if (StHexEncode (Payload + Offset, ChunkSize, Hex,
                     sizeof (Hex) / sizeof (Hex[0])) != EFI_SUCCESS) {
      AtReportAdd (Report, L"policy.raw_error=encoding_failed");
      return;
    }
    AtReportAdd (Report, L"policy.raw.%04x=%s", (UINT32)Offset, Hex);
  }
}

STATIC VOID
AddSecureNotRunRows (
  IN OUT AT_REPORT    *Report,
  IN     CONST CHAR16 *State
  )
{
  AtReportAdd (Report, L"secure.state=%s", State);
  AtReportAdd (Report, L"secure.transport_status=not_run");
  AtReportAdd (Report, L"secure.common_status=unknown");
  AtReportAdd (Report, L"secure.status_0=unknown");
  AtReportAdd (Report, L"secure.status_1=unknown");
  AtReportAdd (Report, L"secure.production=unknown");
  AtReportAdd (Report, L"secure.debug_disabled=unknown");
  AtReportAdd (Report, L"secure.image_cert_debug_disabled=unknown");
  AtReportAdd (Report, L"secure.secure_device=unknown");
}

STATIC VOID
AddSecureRows (
  IN OUT AT_REPORT *Report,
  IN     EFI_STATUS TransportStatus,
  IN     CONST UINT64 Results[SCM_MAX_NUM_RESULTS]
  )
{
  ST_SECURE_PREDICATES Predicates;
  BOOLEAN Complete;

  Complete = (BOOLEAN)(TransportStatus == EFI_SUCCESS && Results[0] == 1);
  AtReportAdd (Report, L"secure.state=%s",
               CallState (TransportStatus, Complete));
  AtReportAdd (Report, L"secure.transport_status=0x%016lx",
               (UINT64)TransportStatus);
  if (TransportStatus == EFI_SUCCESS) {
    AtReportAdd (Report, L"secure.common_status=0x%016lx", Results[0]);
    AtReportAdd (Report, L"secure.status_0=0x%016lx", Results[1]);
    AtReportAdd (Report, L"secure.status_1=0x%016lx", Results[2]);
  } else {
    AtReportAdd (Report, L"secure.common_status=unknown");
    AtReportAdd (Report, L"secure.status_0=unknown");
    AtReportAdd (Report, L"secure.status_1=unknown");
  }
  if (!Complete) {
    AtReportAdd (Report, L"secure.production=unknown");
    AtReportAdd (Report, L"secure.debug_disabled=unknown");
    AtReportAdd (Report, L"secure.image_cert_debug_disabled=unknown");
    AtReportAdd (Report, L"secure.secure_device=unknown");
    return;
  }
  StDecodeSecurePredicates (Results[1], Results[2], &Predicates);
  AtReportAdd (Report, L"secure.production=%s", BoolName (Predicates.Production));
  AtReportAdd (Report, L"secure.debug_disabled=%s", BoolName (Predicates.DebugDisabled));
  AtReportAdd (Report, L"secure.image_cert_debug_disabled=%s",
               BoolName (Predicates.ImageCertDebugDisabled));
  AtReportAdd (Report, L"secure.secure_device=%s", BoolName (Predicates.SecureDevice));
}

typedef struct {
  EFI_STATUS     SetupStatus;
  BOOLEAN        Invoked;
  EFI_STATUS     TransportStatus;
  UINT64         Result0;
  BOOLEAN        GuardIntact;
  EFI_STATUS     ParseStatus;
  UINT32         Magic;
  UINT32         Size;
  UINT32         Revision;
  ST_POLICY_VIEW View;
  UINTN          PayloadSize;
  UINT8          Payload[ST_POLICY_SIZE_REVISION_5];
} ST_POLICY_PROBE_RESULT;

STATIC VOID
CollectPolicyPayload (
  IN  QCOM_SCM_PROTOCOL     *Scm,
  OUT ST_POLICY_PROBE_RESULT *Result
  )
{
  EFI_PHYSICAL_ADDRESS Address;
  UINT8 *Buffer;
  UINT8 *Canary;
  UINT64 Parameters[SCM_MAX_NUM_PARAMETERS];
  UINT64 Results[SCM_MAX_NUM_RESULTS];
  UINTN PayloadSize;
  ST_POLICY_LAYOUT Layout;
  EFI_STATUS Status;
  UINTN Index;

  ZeroMem (Result, sizeof (*Result));
  Result->SetupStatus = EFI_SUCCESS;
  Result->TransportStatus = EFI_NOT_READY;
  Result->ParseStatus = EFI_NOT_READY;
  Layout = StPolicyLayoutForScmRevision (Scm->Revision);
  PayloadSize = StPolicyExpectedSize (Layout);
  if (PayloadSize == 0 || PayloadSize > sizeof (Result->Payload) ||
      PayloadSize > EFI_PAGE_SIZE - ST_POLICY_CANARY_BYTES) {
    Result->SetupStatus = EFI_UNSUPPORTED;
    return;
  }

  Address = 0;
  Status = gBS->AllocatePages (AllocateAnyPages, EfiBootServicesData, 1, &Address);
  if (EFI_ERROR (Status)) {
    Result->SetupStatus = Status;
    return;
  }
  Buffer = (UINT8 *)(UINTN)Address;
  SetMem (Buffer, EFI_PAGE_SIZE, 0);
  Canary = Buffer + PayloadSize;
  SetMem (Canary, ST_POLICY_CANARY_BYTES, mCanaryByte);
  SetMem (Parameters, sizeof (Parameters), 0);
  SetMem (Results, sizeof (Results), 0);
  Parameters[0] = (UINT64)(UINTN)Buffer;
  Parameters[1] = (UINT64)PayloadSize;
  (VOID)WriteBackInvalidateDataCacheRange (
          Buffer, PayloadSize + ST_POLICY_CANARY_BYTES);
  Result->Invoked = TRUE;
  Status = Scm->ScmSipSysCall (
                  Scm, ST_SCM_POLICY_ID, ST_SCM_POLICY_PARAM_ID,
                  Parameters, Results);
  (VOID)InvalidateDataCacheRange (
          Buffer, PayloadSize + ST_POLICY_CANARY_BYTES);
  Result->TransportStatus = Status;
  Result->Result0 = Results[0];

  Result->GuardIntact = TRUE;
  for (Index = 0; Index < ST_POLICY_CANARY_BYTES; Index++) {
    if (Canary[Index] != mCanaryByte) {
      Result->GuardIntact = FALSE;
      break;
    }
  }
  if (Status == EFI_SUCCESS) {
    Result->PayloadSize = PayloadSize;
    CopyMem (Result->Payload, Buffer, PayloadSize);
    Result->Magic = (UINT32)Buffer[0] | ((UINT32)Buffer[1] << 8) |
                    ((UINT32)Buffer[2] << 16) | ((UINT32)Buffer[3] << 24);
    Result->Size = (UINT32)Buffer[4] | ((UINT32)Buffer[5] << 8) |
                   ((UINT32)Buffer[6] << 16) | ((UINT32)Buffer[7] << 24);
    Result->Revision = (UINT32)Buffer[8] | ((UINT32)Buffer[9] << 8) |
                       ((UINT32)Buffer[10] << 16) | ((UINT32)Buffer[11] << 24);
    Result->ParseStatus = Result->GuardIntact ?
      StParsePolicy (Buffer, PayloadSize, Scm->Revision, &Result->View) :
      EFI_COMPROMISED_DATA;
  }
  (VOID)gBS->FreePages (Address, 1);
}

STATIC VOID
AddPolicyNotRunRows (
  IN OUT AT_REPORT    *Report,
  IN     CONST CHAR16 *State
  )
{
  AtReportAdd (Report, L"policy.state=%s", State);
  AtReportAdd (Report, L"policy.setup_status=not_run");
  AtReportAdd (Report, L"policy.transport_status=not_run");
  AtReportAdd (Report, L"policy.result_0=unknown");
  AtReportAdd (Report, L"policy.buffer_guard=not_run");
  AtReportAdd (Report, L"policy.parse_status=not_run");
  AtReportAdd (Report, L"policy.magic=unknown");
  AtReportAdd (Report, L"policy.size=unknown");
  AtReportAdd (Report, L"policy.revision=unknown");
  AddUnknownPolicyRows (Report, L"unknown");
  AddPolicyPayloadRows (Report, NULL, 0);
}

BOOLEAN
StScmProtocolSupportsSip (IN CONST QCOM_SCM_PROTOCOL *Protocol)
{
  return (BOOLEAN)(Protocol != NULL &&
                   StIsSupportedScmRevision (Protocol->Revision) &&
                   Protocol->ScmSipSysCall != NULL);
}

EFI_STATUS
StCollectScmPolicy (IN OUT AT_REPORT *Report)
{
  QCOM_SCM_PROTOCOL *Scm;
  EFI_STATUS Status;
  EFI_STATUS SecureStatus;
  UINT64 SecureResults[SCM_MAX_NUM_RESULTS];
  UINT64 SecureParameters[SCM_MAX_NUM_PARAMETERS];
  ST_POLICY_PROBE_RESULT Policy;
  CONST CHAR16 *UnavailableState;

  if (Report == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Scm = NULL;
  Status = gBS->LocateProtocol (&gQcomScmProtocolGuid, NULL, (VOID **)&Scm);
  if (EFI_ERROR (Status) || Scm == NULL) {
    AtReportAdd (Report, L"scm.state=absent");
    AtReportAdd (Report, L"scm.revision=unknown");
    AddSecureNotRunRows (Report, L"absent");
    AddPolicyNotRunRows (Report, L"absent");
    return EFI_SUCCESS;
  }
  AtReportAdd (Report, L"scm.revision=0x%016lx", Scm->Revision);
  if (!StScmProtocolSupportsSip (Scm)) {
    UnavailableState = StIsSupportedScmRevision (Scm->Revision) ?
                       L"present" : L"unsupported";
    AtReportAdd (Report, L"scm.state=%s", UnavailableState);
    AddSecureNotRunRows (Report, UnavailableState);
    AddPolicyNotRunRows (Report, UnavailableState);
    return EFI_SUCCESS;
  }

  AtReportAdd (Report, L"scm.state=callable");
  SetMem (SecureParameters, sizeof (SecureParameters), 0);
  SetMem (SecureResults, sizeof (SecureResults), 0);
  SecureStatus = Scm->ScmSipSysCall (
                       Scm, ST_SCM_SECURE_STATE_ID,
                       ST_SCM_SECURE_STATE_PARAM_ID,
                       SecureParameters, SecureResults);
  AddSecureRows (Report, SecureStatus, SecureResults);

  CollectPolicyPayload (Scm, &Policy);
  AtReportAdd (Report, L"policy.state=%s",
               Policy.Invoked ?
               CallState (Policy.TransportStatus,
                          (BOOLEAN)(Policy.TransportStatus == EFI_SUCCESS)) :
               L"error");
  AtReportAdd (Report, L"policy.setup_status=0x%016lx",
               (UINT64)Policy.SetupStatus);
  if (Policy.Invoked) {
    AtReportAdd (Report, L"policy.transport_status=0x%016lx",
                 (UINT64)Policy.TransportStatus);
    AtReportAdd (Report, L"policy.result_0=0x%016lx", Policy.Result0);
    AtReportAdd (Report, L"policy.buffer_guard=%s",
                 Policy.GuardIntact ? L"ok" : L"corrupt");
  } else {
    AtReportAdd (Report, L"policy.transport_status=not_run");
    AtReportAdd (Report, L"policy.result_0=unknown");
    AtReportAdd (Report, L"policy.buffer_guard=not_run");
  }
  if (Policy.Invoked && Policy.TransportStatus == EFI_SUCCESS) {
    AtReportAdd (Report, L"policy.parse_status=0x%016lx",
                 (UINT64)Policy.ParseStatus);
    AtReportAdd (Report, L"policy.magic=0x%08x", Policy.Magic);
    AtReportAdd (Report, L"policy.size=%u", Policy.Size);
    AtReportAdd (Report, L"policy.revision=%u", Policy.Revision);
  } else {
    AtReportAdd (Report, L"policy.parse_status=not_run");
    AtReportAdd (Report, L"policy.magic=unknown");
    AtReportAdd (Report, L"policy.size=unknown");
    AtReportAdd (Report, L"policy.revision=unknown");
  }
  if (Policy.ParseStatus == EFI_SUCCESS && Policy.View.Valid) {
    AddPolicyRows (Report, &Policy.View);
  } else {
    AddUnknownPolicyRows (
      Report,
      (Policy.Invoked && Policy.TransportStatus == EFI_SUCCESS) ?
      L"false" : L"unknown");
  }
  AddPolicyPayloadRows (Report, Policy.Payload, Policy.PayloadSize);
  return EFI_SUCCESS;
}
