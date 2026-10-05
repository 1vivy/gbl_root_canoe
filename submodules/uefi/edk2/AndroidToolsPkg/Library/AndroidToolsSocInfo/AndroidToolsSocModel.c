/** @file
 *  Pure Qualcomm raw-chip-ID classification.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Library/AndroidToolsSocInfo.h>

STATIC BOOLEAN
AtSocAsciiEqual (
  IN CONST CHAR8 *Left,
  IN CONST CHAR8 *Right
  )
{
  if (Left == NULL || Right == NULL) {
    return FALSE;
  }
  while (*Left != '\0' && *Left == *Right) {
    Left++;
    Right++;
  }
  return (BOOLEAN)(*Left == *Right);
}

AT_SOC_KIND
AtSocKindFromRawChipId (
  IN UINT32 RawChipId
  )
{
  /* Upper qcom,msm-id variant bits do not change the product ID. The bundled
     DTs supply the Alor/Canoe family IDs; 0x2fd is a live ChipInfo raw ID from
     an SM8845 Macan device. */
  switch (RawChipId & 0xffffu) {
  case 0x02adu:
  case 0x02c0u:
  case 0x02d7u:
  case 0x02fdu:
    return AtSocSm8845;
  case 0x0294u:
  case 0x0295u:
    return AtSocSm8850;
  default:
    return AtSocUnknown;
  }
}

AT_SOC_KIND
AtSocKindFromChipIdString (
  IN CONST CHAR8 *ChipIdString
  )
{
  if (AtSocAsciiEqual (ChipIdString, "SM8845")) {
    return AtSocSm8845;
  }
  if (AtSocAsciiEqual (ChipIdString, "SM8850")) {
    return AtSocSm8850;
  }
  return AtSocUnknown;
}

CONST CHAR16 *
AtSocKindName (
  IN AT_SOC_KIND Kind
  )
{
  switch (Kind) {
  case AtSocSm8845:  return L"SM8845";
  case AtSocSm8850:  return L"SM8850";
  default:           return L"unsupported/unknown";
  }
}
