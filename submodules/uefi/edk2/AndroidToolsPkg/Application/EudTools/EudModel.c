/** @file
 *  Pure EUD result and frame classification.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include "EudTools.h"

EUD_SECURE_OUTCOME
EudClassifySecureResult (
  IN BOOLEAN               Attempted,
  IN CONST EUD_SCM_RESULT *Result
  )
{
  if (!Attempted) {
    return EudSecureNotRun;
  }
  if (Result == NULL || EFI_ERROR (Result->TransportStatus)) {
    return EudSecureTransportError;
  }
  return (Result->Results[0] == 1u)
         ? EudSecureAccepted : EudSecureRejected;
}

BOOLEAN
EudComFrameValid (
  IN UINT32 Id,
  IN UINT32 Length
  )
{
  return (BOOLEAN)(Id == EUD_COM_EXECUTION_ID &&
                   Length <= EUD_COM_MAX_PAYLOAD);
}

CONST CHAR16 *
EudStageName (
  IN EUD_STAGE Stage
  )
{
  switch (Stage) {
  case EudStageIdle:                 return L"idle";
  case EudStageEvidenceOpen:         return L"evidence-open";
  case EudStageMinidumpDiscover:     return L"minidump-discover";
  case EudStageMinidumpClaim:        return L"minidump-claim";
  case EudStageSecureReadBefore:     return L"secure-read-before";
  case EudStageSecureWriteEnable:    return L"secure-write-enable";
  case EudStageSecureReadAfter:      return L"secure-read-after";
  case EudStageUtmiProgram:          return L"utmi-program";
  case EudStageCsrEnable:            return L"csr-enable";
  case EudStageAttach:               return L"attach";
  case EudStageComTest:              return L"com-test";
  case EudStageRestoreNonsecure:     return L"restore-nonsecure";
  case EudStageSecureRestore:        return L"secure-restore";
  case EudStageMinidumpRelease:      return L"minidump-release";
  case EudStageComplete:             return L"complete";
  default:                           return L"unknown";
  }
}
