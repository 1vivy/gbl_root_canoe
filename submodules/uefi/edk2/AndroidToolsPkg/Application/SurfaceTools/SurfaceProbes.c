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

STATIC VOID
AddUnavailable (
  IN OUT AT_REPORT   *Report,
  IN     CONST CHAR16 *Name,
  IN     BOOLEAN      Present,
  IN     BOOLEAN      MethodPresent
  )
{
  ST_PROBE_OBSERVATION Observation;
  Observation.Present = Present;
  Observation.MethodPresent = MethodPresent;
  Observation.Invoked = FALSE;
  Observation.Status = EFI_NOT_READY;
  Observation.EffectObserved = FALSE;
  AtReportAdd (Report, L"%s: %s", Name,
               StProbeStateName (StClassifyProbe (&Observation)));
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
  AtReportAdd (Report, L"%s: %s (%r)%s%s", Name,
               StProbeStateName (StClassifyProbe (&Observation)), Status,
               (Status == EFI_SUCCESS && Value != NULL) ? L" value=" : L"",
               (Status == EFI_SUCCESS && Value != NULL) ? Value : L"");
}

STATIC VOID
AddUnsupportedRevision (
  IN OUT AT_REPORT    *Report,
  IN     CONST CHAR16 *Name,
  IN     UINT64        Revision
  )
{
  AtReportAdd (Report, L"%s: present; unsupported ABI rev=%lx",
               Name, Revision);
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

  Status = AtReportInit (Report, 8);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  AtReportAdd (Report, L"authorized=EFI_SUCCESS; effectiveness not inferred");

  Debug = NULL;
  Status = gBS->LocateProtocol (&gEfiDebugSupportProtocolGuid, NULL,
                                (VOID **)&Debug);
  if (EFI_ERROR (Status) || Debug == NULL ||
      Debug->GetMaximumProcessorIndex == NULL) {
    AddUnavailable (Report, L"DebugSupport.GetMaxProcessorIndex",
                    (BOOLEAN)(!EFI_ERROR (Status) && Debug != NULL),
                    (BOOLEAN)(Debug != NULL &&
                              Debug->GetMaximumProcessorIndex != NULL));
  } else {
    MaxProcessor = 0;
    Status = Debug->GetMaximumProcessorIndex (Debug, &MaxProcessor);
    UnicodeSPrint (Value, sizeof (Value), L"%Lu", (UINT64)MaxProcessor);
    AddResult (Report, L"DebugSupport.GetMaxProcessorIndex", Status, Value);
  }

  Scm = NULL;
  Status = gBS->LocateProtocol (&gQcomScmProtocolGuid, NULL, (VOID **)&Scm);
  if (EFI_ERROR (Status) || Scm == NULL) {
    AddUnavailable (Report, L"SCM.GetVersion", FALSE, FALSE);
  } else if (Scm->Revision != QCOM_SCM_PROTOCOL_REVISION) {
    AddUnsupportedRevision (Report, L"SCM.GetVersion", Scm->Revision);
  } else if (Scm->ScmGetVersion == NULL) {
    AddUnavailable (Report, L"SCM.GetVersion", TRUE, FALSE);
  } else {
    Version = 0;
    Status = Scm->ScmGetVersion (Scm, &Version);
    UnicodeSPrint (Value, sizeof (Value), L"0x%08x", Version);
    AddResult (Report, L"SCM.GetVersion", Status, Value);
  }

  Vb = NULL;
  Status = gBS->LocateProtocol (&gEfiQcomVerifiedBootProtocolGuid, NULL,
                                (VOID **)&Vb);
  if (EFI_ERROR (Status) || Vb == NULL) {
    AddUnavailable (Report, L"VB.IsDeviceSecure", FALSE, FALSE);
    AddUnavailable (Report, L"VB.GetBootState", FALSE, FALSE);
    AddUnavailable (Report, L"VB.IsKeymasterEnabled", FALSE, FALSE);
    return EFI_SUCCESS;
  }
  if (Vb->Revision != QCOM_VERIFIEDBOOT_PROTOCOL_REVISION) {
    AddUnsupportedRevision (Report, L"VB.IsDeviceSecure", Vb->Revision);
    AddUnsupportedRevision (Report, L"VB.GetBootState", Vb->Revision);
    AddUnsupportedRevision (Report, L"VB.IsKeymasterEnabled", Vb->Revision);
    return EFI_SUCCESS;
  }

  if (Vb->VBIsDeviceSecure == NULL) {
    AddUnavailable (Report, L"VB.IsDeviceSecure", TRUE, FALSE);
  } else {
    Flag = FALSE;
    Status = Vb->VBIsDeviceSecure (Vb, &Flag);
    UnicodeSPrint (Value, sizeof (Value), L"%s", Flag ? L"true" : L"false");
    AddResult (Report, L"VB.IsDeviceSecure", Status, Value);
  }

  if (Vb->VBGetBootState == NULL) {
    AddUnavailable (Report, L"VB.GetBootState", TRUE, FALSE);
  } else {
    BootState = BOOT_STATE_MAX;
    Status = Vb->VBGetBootState (Vb, &BootState);
    UnicodeSPrint (Value, sizeof (Value), L"%u", (UINT32)BootState);
    AddResult (Report, L"VB.GetBootState", Status, Value);
  }

  if (Vb->VBIsKeymasterEnabled == NULL) {
    AddUnavailable (Report, L"VB.IsKeymasterEnabled", TRUE, FALSE);
  } else {
    Flag = FALSE;
    Status = Vb->VBIsKeymasterEnabled (Vb, &Flag);
    UnicodeSPrint (Value, sizeof (Value), L"%s", Flag ? L"true" : L"false");
    AddResult (Report, L"VB.IsKeymasterEnabled", Status, Value);
  }

  return EFI_SUCCESS;
}
