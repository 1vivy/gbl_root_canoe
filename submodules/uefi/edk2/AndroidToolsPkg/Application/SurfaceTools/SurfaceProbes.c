/** @file
 *  Explicit, confirmation-gated read-only calls used to classify whether a
 *  discovered surface is merely present, ABI-callable, authorized or effective.
 *
 *  The caller must obtain a fresh operator confirmation before entering this
 *  file. No arbitrary SCM IDs, QSEE/SPSS methods, writes or USB role changes are
 *  exposed here.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/DebugSupport.h>
#include <Protocol/EFIScm.h>
#include <Protocol/EFIVerifiedBoot.h>

#include "SurfaceInventory.h"
#include "SurfacePolicy.h"
#include "SurfaceScmProbes.h"

STATIC CONST CHAR16 *
ProbeStateName (
  IN ST_PROBE_STATE State
  )
{
  return (State == StProbeCallable) ? L"callable" : StProbeStateName (State);
}

STATIC VOID
AddUnavailable (
  IN OUT AT_REPORT    *Report,
  IN     CONST CHAR16 *Name,
  IN     BOOLEAN       Present,
  IN     BOOLEAN       MethodPresent
  )
{
  ST_PROBE_OBSERVATION Observation;
  Observation.Present = Present;
  Observation.MethodPresent = MethodPresent;
  Observation.Invoked = FALSE;
  Observation.Status = EFI_NOT_READY;
  Observation.EffectObserved = FALSE;
  AtReportAdd (Report, L"%s.state=%s", Name,
               ProbeStateName (StClassifyProbe (&Observation)));
  AtReportAdd (Report, L"%s.status=not_run", Name);
  AtReportAdd (Report, L"%s.value=unknown", Name);
}

STATIC VOID
AddResult (
  IN OUT AT_REPORT    *Report,
  IN     CONST CHAR16 *Name,
  IN     EFI_STATUS    Status,
  IN     CONST CHAR16 *Value
  )
{
  ST_PROBE_OBSERVATION Observation;
  Observation.Present = TRUE;
  Observation.MethodPresent = TRUE;
  Observation.Invoked = TRUE;
  Observation.Status = Status;
  Observation.EffectObserved = FALSE;
  AtReportAdd (Report, L"%s.state=%s", Name,
               ProbeStateName (StClassifyProbe (&Observation)));
  AtReportAdd (Report, L"%s.status=0x%016lx", Name, (UINT64)Status);
  AtReportAdd (Report, L"%s.value=%s", Name,
               (Status == EFI_SUCCESS && Value != NULL) ? Value : L"unknown");
}

STATIC VOID
AddUnsupportedRevision (
  IN OUT AT_REPORT    *Report,
  IN     CONST CHAR16 *Name,
  IN     UINT64        Revision
  )
{
  AtReportAdd (Report, L"%s.state=unsupported", Name);
  AtReportAdd (Report, L"%s.status=not_run", Name);
  AtReportAdd (Report, L"%s.revision=0x%016lx", Name, Revision);
  AtReportAdd (Report, L"%s.value=unknown", Name);
}


EFI_STATUS
StBuildProbeReport (OUT AT_REPORT *Report)
{
  EFI_DEBUG_SUPPORT_PROTOCOL *Debug;
  QCOM_SCM_PROTOCOL *Scm;
  QCOM_VERIFIEDBOOT_PROTOCOL *Vb;
  EFI_STATUS Status;
  UINTN MaxProcessor;
  UINT32 Version;
  BOOLEAN Flag;
  boot_state_t BootState;
  CHAR16 Value[32];

  Status = AtReportInit (Report, 128);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  AtReportAdd (Report, L"effectiveness=not_observed");

  Debug = NULL;
  Status = gBS->LocateProtocol (&gEfiDebugSupportProtocolGuid, NULL,
                                (VOID **)&Debug);
  if (EFI_ERROR (Status) || Debug == NULL ||
      Debug->GetMaximumProcessorIndex == NULL) {
    AddUnavailable (Report, L"debugsupport.get_max_processor_index",
                    (BOOLEAN)(!EFI_ERROR (Status) && Debug != NULL),
                    (BOOLEAN)(Debug != NULL &&
                              Debug->GetMaximumProcessorIndex != NULL));
  } else {
    MaxProcessor = 0;
    Status = Debug->GetMaximumProcessorIndex (Debug, &MaxProcessor);
    UnicodeSPrint (Value, sizeof (Value), L"%Lu", (UINT64)MaxProcessor);
    AddResult (Report, L"debugsupport.get_max_processor_index", Status, Value);
  }

  Scm = NULL;
  Status = gBS->LocateProtocol (&gQcomScmProtocolGuid, NULL, (VOID **)&Scm);
  if (EFI_ERROR (Status) || Scm == NULL) {
    AddUnavailable (Report, L"scm.get_version", FALSE, FALSE);
  } else if (!StIsSupportedScmRevision (Scm->Revision)) {
    AddUnsupportedRevision (Report, L"scm.get_version", Scm->Revision);
  } else if (Scm->ScmGetVersion == NULL) {
    AddUnavailable (Report, L"scm.get_version", TRUE, FALSE);
  } else {
    Version = 0;
    Status = Scm->ScmGetVersion (Scm, &Version);
    UnicodeSPrint (Value, sizeof (Value), L"0x%08x", Version);
    AddResult (Report, L"scm.get_version", Status, Value);
  }
  (VOID)StCollectScmPolicy (Report);

  Vb = NULL;
  Status = gBS->LocateProtocol (&gEfiQcomVerifiedBootProtocolGuid, NULL,
                                (VOID **)&Vb);
  if (EFI_ERROR (Status) || Vb == NULL) {
    AddUnavailable (Report, L"vb.is_device_secure", FALSE, FALSE);
    AddUnavailable (Report, L"vb.get_boot_state", FALSE, FALSE);
    AddUnavailable (Report, L"vb.is_keymaster_enabled", FALSE, FALSE);
    return EFI_SUCCESS;
  }
  if (Vb->Revision != QCOM_VERIFIEDBOOT_PROTOCOL_REVISION) {
    AddUnsupportedRevision (Report, L"vb.is_device_secure", Vb->Revision);
    AddUnsupportedRevision (Report, L"vb.get_boot_state", Vb->Revision);
    AddUnsupportedRevision (Report, L"vb.is_keymaster_enabled", Vb->Revision);
    return EFI_SUCCESS;
  }

  if (Vb->VBIsDeviceSecure == NULL) {
    AddUnavailable (Report, L"vb.is_device_secure", TRUE, FALSE);
  } else {
    Flag = FALSE;
    Status = Vb->VBIsDeviceSecure (Vb, &Flag);
    UnicodeSPrint (Value, sizeof (Value), L"%s", Flag ? L"true" : L"false");
    AddResult (Report, L"vb.is_device_secure", Status, Value);
  }

  if (Vb->VBGetBootState == NULL) {
    AddUnavailable (Report, L"vb.get_boot_state", TRUE, FALSE);
  } else {
    BootState = BOOT_STATE_MAX;
    Status = Vb->VBGetBootState (Vb, &BootState);
    UnicodeSPrint (Value, sizeof (Value), L"%u", (UINT32)BootState);
    AddResult (Report, L"vb.get_boot_state", Status, Value);
  }

  if (Vb->VBIsKeymasterEnabled == NULL) {
    AddUnavailable (Report, L"vb.is_keymaster_enabled", TRUE, FALSE);
  } else {
    Flag = FALSE;
    Status = Vb->VBIsKeymasterEnabled (Vb, &Flag);
    UnicodeSPrint (Value, sizeof (Value), L"%s", Flag ? L"true" : L"false");
    AddResult (Report, L"vb.is_keymaster_enabled", Status, Value);
  }

  return EFI_SUCCESS;
}
