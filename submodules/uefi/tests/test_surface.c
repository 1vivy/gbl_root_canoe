/*
 * Host regression for SurfaceTools' evidence states and bounded paging.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <assert.h>
#include <stdio.h>
#undef NULL


#include "../edk2/AndroidToolsPkg/Application/SurfaceTools/SurfaceModel.h"

static ST_PROBE_OBSERVATION
Observation (
  BOOLEAN Present,
  BOOLEAN MethodPresent,
  BOOLEAN Invoked,
  EFI_STATUS Status,
  BOOLEAN EffectObserved
  )
{
  ST_PROBE_OBSERVATION Result;
  Result.Present = Present;
  Result.MethodPresent = MethodPresent;
  Result.Invoked = Invoked;
  Result.Status = Status;
  Result.EffectObserved = EffectObserved;
  return Result;
}

static void
TestEvidenceStatesDoNotOverclaim (void)
{
  ST_PROBE_OBSERVATION O;

  assert (StClassifyProbe (NULL) == StProbeAbsent);
  O = Observation (FALSE, FALSE, FALSE, EFI_NOT_FOUND, FALSE);
  assert (StClassifyProbe (&O) == StProbeAbsent);

  O = Observation (TRUE, FALSE, FALSE, EFI_NOT_READY, FALSE);
  assert (StClassifyProbe (&O) == StProbePresent);
  O = Observation (TRUE, TRUE, FALSE, EFI_NOT_READY, FALSE);
  assert (StClassifyProbe (&O) == StProbeCallable);

  O = Observation (TRUE, TRUE, TRUE, EFI_ACCESS_DENIED, FALSE);
  assert (StClassifyProbe (&O) == StProbeDenied);
  O.Status = EFI_SECURITY_VIOLATION;
  assert (StClassifyProbe (&O) == StProbeDenied);
  O.Status = EFI_UNSUPPORTED;
  assert (StClassifyProbe (&O) == StProbeUnsupported);
  O.Status = EFI_DEVICE_ERROR;
  assert (StClassifyProbe (&O) == StProbeError);
  O.Status = EFI_WARN_UNKNOWN_GLYPH;
  assert (StClassifyProbe (&O) == StProbeError);


  /* A successful read-only query proves authorization and a returned value,
   * not exploit effectiveness. Effectiveness needs a separately observed
   * consequence and is never inferred by the current probes. */
  O = Observation (TRUE, TRUE, TRUE, EFI_SUCCESS, FALSE);
  assert (StClassifyProbe (&O) == StProbeAuthorized);
  O.EffectObserved = TRUE;
  assert (StClassifyProbe (&O) == StProbeEffective);
}

static void
TestPagingIsBoundedAndStable (void)
{
  assert (StMovePage (0, 0, 10, TRUE) == 0);
  assert (StMovePage (0, 20, 0, TRUE) == 0);
  assert (StMovePage (0, 25, 10, TRUE) == 10);
  assert (StMovePage (10, 25, 10, TRUE) == 20);
  assert (StMovePage (20, 25, 10, TRUE) == 20);
  assert (StMovePage (20, 25, 10, FALSE) == 10);
  assert (StMovePage (0, 25, 10, FALSE) == 0);

  /* A stale page offset clamps to the final real page before movement. */
  assert (StMovePage (999, 25, 10, TRUE) == 20);
  assert (StMovePage (999, 25, 10, FALSE) == 10);

  /* Count-Start subtraction prevents Start+Rows overflow. */
  assert (StMovePage (MAX_UINTN - 1, MAX_UINTN, 10, TRUE) == MAX_UINTN - 1);
}

int
main (void)
{
  TestEvidenceStatesDoNotOverclaim ();
  TestPagingIsBoundedAndStable ();
  puts ("surface model tests passed");
  return 0;
}
