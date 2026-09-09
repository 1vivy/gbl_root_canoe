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
#include "../edk2/AndroidToolsPkg/Application/SurfaceTools/SurfacePolicy.h"

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

static void
Put32 (
  unsigned char *Data,
  size_t         Offset,
  unsigned         Value
  )
{
  Data[Offset] = (unsigned char)Value;
  Data[Offset + 1] = (unsigned char)(Value >> 8);
  Data[Offset + 2] = (unsigned char)(Value >> 16);
  Data[Offset + 3] = (unsigned char)(Value >> 24);
}

static void
Put64 (
  unsigned char *Data,
  size_t         Offset,
  UINT64         Value
  )
{
  size_t Index;
  for (Index = 0; Index < sizeof (UINT64); Index++) {
    Data[Offset + Index] = (unsigned char)(Value >> (Index * 8));
  }
}

static void
TestPolicyRevisionSelection (void)
{
  assert (StIsSupportedScmRevision (ST_SCM_REVISION_2));
  assert (StIsSupportedScmRevision (ST_SCM_REVISION_5));
  assert (!StIsSupportedScmRevision (0x50001));
  assert (StPolicyLayoutForScmRevision (ST_SCM_REVISION_2) ==
          StPolicyLayoutRevision2);
  assert (StPolicyLayoutForScmRevision (ST_SCM_REVISION_5) ==
          StPolicyLayoutRevision5);
  assert (StPolicyLayoutForScmRevision (0) == StPolicyLayoutUnknown);
  assert (StPolicyExpectedSize (StPolicyLayoutRevision2) == 960);
  assert (StPolicyExpectedSize (StPolicyLayoutRevision5) == 1220);
  assert (StPolicyExpectedRevision (StPolicyLayoutRevision2) == 2);
  assert (StPolicyExpectedRevision (StPolicyLayoutRevision5) == 5);
}

static void
TestPolicyValidLayouts (void)
{
  unsigned char Revision2[ST_POLICY_SIZE_REVISION_2] = { 0 };
  unsigned char Revision5[ST_POLICY_SIZE_REVISION_5] = { 0 };
  ST_POLICY_VIEW View;
  EFI_STATUS Status;

  Put32 (Revision2, 0, 0x12345678);
  Put32 (Revision2, 4, ST_POLICY_SIZE_REVISION_2);
  Put32 (Revision2, 8, ST_POLICY_REVISION_2);
  Put64 (Revision2, 12, 0x0f);
  Put32 (Revision2, 20, 0x00000005);
  Put32 (Revision2, 24, 4);
  Put32 (Revision2, 156, 200);
  Status = StParsePolicy (Revision2, sizeof (Revision2), ST_SCM_REVISION_2,
                          &View);
  assert (Status == EFI_SUCCESS);
  assert (View.Valid && View.Layout == StPolicyLayoutRevision2);
  assert (View.Magic == 0x12345678 && View.RootCount == 4);
  assert (View.SerialCount == 200 && View.QcRootCount == 0);

  Put32 (Revision5, 0, 0x87654321);
  Put32 (Revision5, 4, ST_POLICY_SIZE_REVISION_5);
  Put32 (Revision5, 8, ST_POLICY_REVISION_5);
  Put64 (Revision5, 12, (1ULL << 0) | (1ULL << 4) | (1ULL << 7) |
                         (1ULL << 24) | (1ULL << 30));
  Put32 (Revision5, 20, 0x000000a0);
  Put32 (Revision5, 24, 3);
  Put32 (Revision5, 220, 199);
  Put32 (Revision5, 1024, 2);
  Status = StParsePolicy (Revision5, sizeof (Revision5), ST_SCM_REVISION_5,
                          &View);
  assert (Status == EFI_SUCCESS);
  assert (View.Valid && View.Layout == StPolicyLayoutRevision5);
  assert (View.RootCount == 3 && View.SerialCount == 199);
  assert (View.QcRootCount == 2 && View.OemFlags == 0);
}

static void
TestPolicyFailures (void)
{
  unsigned char Revision2[ST_POLICY_SIZE_REVISION_2] = { 0 };
  unsigned char Revision5[ST_POLICY_SIZE_REVISION_5] = { 0 };
  ST_POLICY_VIEW View;

  Put32 (Revision2, 4, ST_POLICY_SIZE_REVISION_2);
  Put32 (Revision2, 8, ST_POLICY_REVISION_2);
  assert (StParsePolicy (Revision2, sizeof (Revision2) - 1,
                         ST_SCM_REVISION_2, &View) == EFI_COMPROMISED_DATA);
  Put32 (Revision2, 4, 959);
  assert (StParsePolicy (Revision2, sizeof (Revision2), ST_SCM_REVISION_2,
                         &View) == EFI_COMPROMISED_DATA);
  Put32 (Revision2, 4, ST_POLICY_SIZE_REVISION_2);
  Put32 (Revision2, 8, 5);
  assert (StParsePolicy (Revision2, sizeof (Revision2), ST_SCM_REVISION_2,
                         &View) == EFI_COMPROMISED_DATA);
  Put32 (Revision2, 8, ST_POLICY_REVISION_2);
  Put32 (Revision2, 24, ST_POLICY_MAX_ROOTS + 1);
  assert (StParsePolicy (Revision2, sizeof (Revision2), ST_SCM_REVISION_2,
                         &View) == EFI_COMPROMISED_DATA);
  Put32 (Revision2, 24, 0);
  Put32 (Revision2, 156, ST_POLICY_MAX_SERIALS + 1);
  assert (StParsePolicy (Revision2, sizeof (Revision2), ST_SCM_REVISION_2,
                         &View) == EFI_COMPROMISED_DATA);
  Put32 (Revision2, 156, 0);
  Put64 (Revision2, 12, 1ULL << 4);
  assert (StParsePolicy (Revision2, sizeof (Revision2), ST_SCM_REVISION_2,
                         &View) == EFI_COMPROMISED_DATA);
  assert (StParsePolicy (Revision2, sizeof (Revision2), 0x40002,
                         &View) == EFI_UNSUPPORTED);

  Put32 (Revision5, 4, ST_POLICY_SIZE_REVISION_5);
  Put32 (Revision5, 8, ST_POLICY_REVISION_5);
  Put32 (Revision5, 24, ST_POLICY_MAX_ROOTS + 1);
  assert (StParsePolicy (Revision5, sizeof (Revision5), ST_SCM_REVISION_5,
                         &View) == EFI_COMPROMISED_DATA);
  Put32 (Revision5, 24, 0);
  Put32 (Revision5, 220, ST_POLICY_MAX_SERIALS + 1);
  assert (StParsePolicy (Revision5, sizeof (Revision5), ST_SCM_REVISION_5,
                         &View) == EFI_COMPROMISED_DATA);
  Put32 (Revision5, 220, 0);
  Put32 (Revision5, 1024, ST_POLICY_MAX_ROOTS + 1);
  assert (StParsePolicy (Revision5, sizeof (Revision5), ST_SCM_REVISION_5,
                         &View) == EFI_COMPROMISED_DATA);
  Put32 (Revision5, 1024, 0);
  Put64 (Revision5, 12, 1ULL << 9);
  assert (StParsePolicy (Revision5, sizeof (Revision5), ST_SCM_REVISION_5,
                         &View) == EFI_COMPROMISED_DATA);
  Put64 (Revision5, 12, 1ULL << ST_POLICY_FLAG_SCHEMA_CONFLICT);
  assert (StParsePolicy (Revision5, sizeof (Revision5), ST_SCM_REVISION_5,
                         &View) == EFI_SUCCESS);
  assert (View.Valid &&
          (View.Flags & (1ULL << ST_POLICY_FLAG_SCHEMA_CONFLICT)) != 0);
  Put64 (Revision5, 12, 1ULL << 32);
  assert (StParsePolicy (Revision5, sizeof (Revision5), ST_SCM_REVISION_5,
                         &View) == EFI_COMPROMISED_DATA);
}

static void
TestPolicyHexEncoding (void)
{
  static const unsigned char Input[] = { 0x00, 0x01, 0xab, 0xff };
  static const CHAR16 Expected[] = {
    L'0', L'0', L'0', L'1', L'a', L'b', L'f', L'f', L'\0'
  };
  CHAR16 Text[sizeof (Expected) / sizeof (Expected[0])];
  size_t Index;

  assert (StHexEncode (Input, sizeof (Input), Text,
                       sizeof (Text) / sizeof (Text[0])) == EFI_SUCCESS);
  for (Index = 0; Index < sizeof (Expected) / sizeof (Expected[0]); Index++) {
    assert (Text[Index] == Expected[Index]);
  }
  assert (StHexEncode (Input, sizeof (Input), Text,
                       sizeof (Text) / sizeof (Text[0]) - 1) ==
          EFI_BUFFER_TOO_SMALL);
  assert (StHexEncode (NULL, 1, Text,
                       sizeof (Text) / sizeof (Text[0])) ==
          EFI_INVALID_PARAMETER);
  assert (StHexEncode (NULL, 0, Text,
                       sizeof (Text) / sizeof (Text[0])) == EFI_SUCCESS);

  {
    unsigned char Chunk[32];
    CHAR16 ChunkText[65];

    for (Index = 0; Index < sizeof (Chunk); Index++) {
      Chunk[Index] = (unsigned char)Index;
    }
    assert (StHexEncode (Chunk, sizeof (Chunk), ChunkText,
                         sizeof (ChunkText) / sizeof (ChunkText[0])) ==
            EFI_SUCCESS);
    assert (ChunkText[0] == L'0' && ChunkText[1] == L'0');
    assert (ChunkText[62] == L'1' && ChunkText[63] == L'f');
    assert (ChunkText[64] == L'\0');
  }
  assert (Text[0] == L'\0');
}

static void
TestSecurePredicates (void)
{
  ST_SECURE_PREDICATES Predicates;

  StDecodeSecurePredicates (1ULL << 6, 0, &Predicates);
  assert (Predicates.Valid && Predicates.Production);
  assert (Predicates.DebugDisabled && Predicates.ImageCertDebugDisabled);
  assert (Predicates.SecureDevice);

  StDecodeSecurePredicates ((1ULL << 0) | (1ULL << 8) | (1ULL << 10),
                            0, &Predicates);
  assert (!Predicates.Production && !Predicates.SecureDevice);
  StDecodeSecurePredicates (1ULL << 2, 0, &Predicates);
  assert (!Predicates.DebugDisabled);
}

int
main (void)
{
  TestEvidenceStatesDoNotOverclaim ();
  TestPagingIsBoundedAndStable ();
  TestPolicyRevisionSelection ();
  TestPolicyValidLayouts ();
  TestPolicyFailures ();
  TestPolicyHexEncoding ();
  TestSecurePredicates ();
  puts ("surface model tests passed");
  return 0;
}
