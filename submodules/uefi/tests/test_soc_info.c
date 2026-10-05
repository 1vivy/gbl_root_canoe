/*
 * Host regression for Qualcomm raw-chip-ID gating.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <assert.h>
#include <stdio.h>
#undef NULL

#include <Library/AndroidToolsSocInfo.h>

static void
TestSm8845Ids (void)
{
  assert (AtSocKindFromRawChipId (0x02ad) == AtSocSm8845);
  assert (AtSocKindFromRawChipId (0x02c0) == AtSocSm8845);
  assert (AtSocKindFromRawChipId (0x02d7) == AtSocSm8845);
  assert (AtSocKindFromRawChipId (0x10002c0) == AtSocSm8845);
}

static void
TestSm8850Ids (void)
{
  assert (AtSocKindFromRawChipId (0x0294) == AtSocSm8850);
  assert (AtSocKindFromRawChipId (0x0295) == AtSocSm8850);
  assert (AtSocKindFromRawChipId (0x1000294) == AtSocSm8850);
  assert (AtSocKindFromRawChipId (0x1010295) == AtSocSm8850);
}

static void
TestUnknownIdsFailClosed (void)
{
  assert (AtSocKindFromRawChipId (0) == AtSocUnknown);
  assert (AtSocKindFromRawChipId (0xffff) == AtSocUnknown);
  assert (AtSocKindFromRawChipId (0x0296) == AtSocUnknown);
}

int
main (void)
{
  TestSm8845Ids ();
  TestSm8850Ids ();
  TestUnknownIdsFailClosed ();
  puts ("soc info: SM8845/SM8850 raw IDs and unknown fail-closed passed");
  return 0;
}
