/** @file
 *  Pure Qualcomm raw-chip-ID classification.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Library/AndroidToolsSocInfo.h>

AT_SOC_KIND
AtSocKindFromRawChipId (
  IN UINT32 RawChipId
  )
{
  /* qcom,msm-id variant prefixes occupy upper bits; the hardware product ID
     remains in the low 16 bits. The IDs below come from the shared Alor/Canoe
     vendor_boot DT bundle captured from the SM8850 device. */
  switch (RawChipId & 0xffffu) {
  case 0x02adu:
  case 0x02c0u:
  case 0x02d7u:
    return AtSocSm8845;
  case 0x0294u:
  case 0x0295u:
    return AtSocSm8850;
  default:
    return AtSocUnknown;
  }
}

CONST CHAR16 *
AtSocKindName (
  IN AT_SOC_KIND Kind
  )
{
  switch (Kind) {
  case AtSocSm8845:  return L"SM8845 (Alor)";
  case AtSocSm8850:  return L"SM8850 (Canoe)";
  default:           return L"unsupported/unknown";
  }
}
