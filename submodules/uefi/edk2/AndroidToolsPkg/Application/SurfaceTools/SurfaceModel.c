/** @file
 *  Pure state and paging model for SurfaceTools.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include "SurfaceModel.h"

ST_PROBE_STATE
StClassifyProbe (
  IN CONST ST_PROBE_OBSERVATION *Observation
  )
{
  if (Observation == NULL || !Observation->Present) {
    return StProbeAbsent;
  }
  if (!Observation->MethodPresent) {
    return StProbePresent;
  }
  if (!Observation->Invoked) {
    return StProbeCallable;
  }
  if (Observation->Status == EFI_ACCESS_DENIED ||
      Observation->Status == EFI_SECURITY_VIOLATION) {
    return StProbeDenied;
  }
  if (Observation->Status == EFI_UNSUPPORTED) {
    return StProbeUnsupported;
  }
  if (Observation->Status != EFI_SUCCESS) {
    return StProbeError;
  }
  return Observation->EffectObserved ? StProbeEffective : StProbeAuthorized;
}

CONST CHAR16 *
StProbeStateName (
  IN ST_PROBE_STATE State
  )
{
  switch (State) {
  case StProbeAbsent:
    return L"absent";
  case StProbePresent:
    return L"present";
  case StProbeCallable:
    return L"callable/not-run";
  case StProbeAuthorized:
    return L"authorized";
  case StProbeEffective:
    return L"effective";
  case StProbeDenied:
    return L"denied";
  case StProbeUnsupported:
    return L"unsupported";
  case StProbeError:
  default:
    return L"error";
  }
}

UINTN
StMovePage (
  IN UINTN   Start,
  IN UINTN   Count,
  IN UINTN   Rows,
  IN BOOLEAN Forward
  )
{
  if (Count == 0 || Rows == 0) {
    return 0;
  }
  if (Start >= Count) {
    Start = ((Count - 1) / Rows) * Rows;
  }
  if (!Forward) {
    return (Start >= Rows) ? Start - Rows : 0;
  }
  if (Rows < Count - Start) {
    return Start + Rows;
  }
  return Start;
}
