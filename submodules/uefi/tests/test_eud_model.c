/*
 * Host regression for EudTools secure-result and COM-frame policy.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#undef NULL

#include "../edk2/AndroidToolsPkg/Application/EudTools/EudTools.h"

static int
Equal16 (const CHAR16 *Left, const CHAR16 *Right)
{
  while (*Left != L'\0' && *Left == *Right) {
    Left++;
    Right++;
  }
  return *Left == *Right;
}


static void
TestSecureResultClassification (void)
{
  EUD_SCM_RESULT Result;

  memset (&Result, 0, sizeof (Result));
  assert (EudClassifySecureResult (FALSE, &Result) == EudSecureNotRun);
  assert (EudClassifySecureResult (TRUE, NULL) == EudSecureTransportError);

  Result.TransportStatus = EFI_ACCESS_DENIED;
  assert (EudClassifySecureResult (TRUE, &Result) ==
          EudSecureTransportError);

  Result.TransportStatus = EFI_SUCCESS;
  Result.Results[0] = 0;
  assert (EudClassifySecureResult (TRUE, &Result) == EudSecureRejected);

  Result.Results[0] = 1;
  assert (EudClassifySecureResult (TRUE, &Result) == EudSecureAccepted);
}

static void
TestComFrameBounds (void)
{
  const UINT32 ExpectedId = 0x90;

  assert (EudComFrameValid (ExpectedId, ExpectedId, 0));
  assert (EudComFrameValid (ExpectedId, ExpectedId, EUD_COM_MAX_PAYLOAD));
  assert (!EudComFrameValid (ExpectedId, ExpectedId,
                             EUD_COM_MAX_PAYLOAD + 1));
  assert (!EudComFrameValid (ExpectedId, 0x81, 1));
}

static void
TestSocProfiles (void)
{
  const EUD_SOC_PROFILE *Sm8845;
  const EUD_SOC_PROFILE *Sm8850;

  Sm8845 = EudProfileForSocKind (AtSocSm8845);
  Sm8850 = EudProfileForSocKind (AtSocSm8850);
  assert (Sm8845 != NULL);
  assert (Sm8850 != NULL);
  assert (EudProfileForSocKind (AtSocUnknown) == NULL);
  assert (Sm8845->SocKind == AtSocSm8845);
  assert (Sm8850->SocKind == AtSocSm8850);
  assert (Sm8845->RegisterBase == 0x088e0000ULL);
  assert (Sm8845->RegisterBase == Sm8850->RegisterBase);
  assert (Sm8845->ModeManagerAddress == Sm8850->ModeManagerAddress);
  assert (Sm8845->ComExecutionId == Sm8850->ComExecutionId);
  assert (Sm8845->UtmiDelayLow == Sm8850->UtmiDelayLow);
  assert (Sm8845->UtmiDelayHigh == Sm8850->UtmiDelayHigh);
  assert (!Sm8845->MinidumpTelemetrySupported);
  assert (Sm8850->MinidumpTelemetrySupported);
}

static void
TestEveryTelemetryStageHasAStableName (void)
{
  EUD_STAGE Stage;

  for (Stage = EudStageIdle; Stage <= EudStageComplete; Stage++) {
    assert (!Equal16 (EudStageName (Stage), L"unknown"));
  }
  assert (Equal16 (EudStageName ((EUD_STAGE)0x7fffffff), L"unknown"));
}

int
main (void)
{
  TestSecureResultClassification ();
  TestComFrameBounds ();
  TestSocProfiles ();
  TestEveryTelemetryStageHasAStableName ();
  puts ("eud model: SoC profiles, secure rejection, COM bounds and telemetry stages passed");
  return 0;
}
