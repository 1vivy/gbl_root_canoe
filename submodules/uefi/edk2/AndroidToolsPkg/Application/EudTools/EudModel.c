/** @file
 *  Pure EUD result and frame classification.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include "EudTools.h"

STATIC CONST EUD_SOC_PROFILE mSm8845Profile = {
  AtSocSm8845,
  L"SM8845",
  0x088E0000ULL,
  0x088E2000ULL,
  0x90u,
  0x00ffu,
  0x0000u,
  FALSE
};

STATIC CONST EUD_SOC_PROFILE mSm8850Profile = {
  AtSocSm8850,
  L"SM8850",
  0x088E0000ULL,
  0x088E2000ULL,
  0x90u,
  0x00ffu,
  0x0000u,
  TRUE
};

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
EudSecureRestoreRequired (
  IN UINT32                BeforeValue,
  IN CONST EUD_SCM_RESULT *WriteResult,
  IN CONST EUD_SCM_RESULT *AfterResult
  )
{
  if (AfterResult != NULL && AfterResult->ValueValid) {
    return (BOOLEAN)(((BeforeValue ^ AfterResult->Value) & 1u) != 0);
  }
  return (BOOLEAN)(
    EudClassifySecureResult (TRUE, WriteResult) == EudSecureAccepted
    );
}

CONST EUD_SOC_PROFILE *
EudProfileForSocKind (
  IN AT_SOC_KIND Kind
  )
{
  switch (Kind) {
  case AtSocSm8845:  return &mSm8845Profile;
  case AtSocSm8850:  return &mSm8850Profile;
  default:           return NULL;
  }
}

UINT32
EudRegisterValue (
  IN UINT32 RawValue
  )
{
  return RawValue & EUD_REGISTER_VALUE_MASK;
}

BOOLEAN
EudComFrameValid (
  IN UINT32 ExpectedId,
  IN UINT32 Id,
  IN UINT32 Length
  )
{
  return (BOOLEAN)(Id == ExpectedId && Length <= EUD_COM_MAX_PAYLOAD);
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
