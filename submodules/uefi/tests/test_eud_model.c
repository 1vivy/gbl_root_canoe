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
  assert (EudComFrameValid (EUD_COM_EXECUTION_ID, 0));
  assert (EudComFrameValid (EUD_COM_EXECUTION_ID, EUD_COM_MAX_PAYLOAD));
  assert (!EudComFrameValid (EUD_COM_EXECUTION_ID,
                             EUD_COM_MAX_PAYLOAD + 1));
  assert (!EudComFrameValid (0x81, 1));
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
  TestEveryTelemetryStageHasAStableName ();
  puts ("eud model: secure rejection, COM bounds and telemetry stages passed");
  return 0;
}
