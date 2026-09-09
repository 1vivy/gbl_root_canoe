/** @file
 *  Pure state and paging model shared by SurfaceTools and its host tests.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __SURFACE_MODEL_H__
#define __SURFACE_MODEL_H__

#include <Uefi.h>

typedef enum {
  StProbeAbsent = 0,
  StProbePresent,
  StProbeCallable,
  StProbeAuthorized,
  StProbeEffective,
  StProbeDenied,
  StProbeUnsupported,
  StProbeError
} ST_PROBE_STATE;

typedef struct {
  BOOLEAN    Present;
  BOOLEAN    MethodPresent;
  BOOLEAN    Invoked;
  EFI_STATUS Status;
  BOOLEAN    EffectObserved;
} ST_PROBE_OBSERVATION;

ST_PROBE_STATE
StClassifyProbe (
  IN CONST ST_PROBE_OBSERVATION *Observation
  );

CONST CHAR16 *
StProbeStateName (
  IN ST_PROBE_STATE State
  );

UINTN
StMovePage (
  IN UINTN   Start,
  IN UINTN   Count,
  IN UINTN   Rows,
  IN BOOLEAN Forward
  );

#endif /* __SURFACE_MODEL_H__ */
