/** @file
 *  Qualcomm SoC identification for platform-gated AndroidTools apps.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __ANDROID_TOOLS_SOC_INFO_H__
#define __ANDROID_TOOLS_SOC_INFO_H__

#include <Uefi.h>
#include <Protocol/EFIChipInfo.h>

typedef enum {
  AtSocUnknown = 0,
  AtSocSm8845,
  AtSocSm8850
} AT_SOC_KIND;

typedef struct {
  EFI_STATUS  LocateStatus;
  EFI_STATUS  RawIdStatus;
  EFI_STATUS  NameStatus;
  UINT64      ProtocolRevision;
  UINT32      RawChipId;
  CHAR8       ChipIdString[EFICHIPINFO_MAX_ID_LENGTH];
  AT_SOC_KIND Kind;
} AT_SOC_INFO;

/** Classify the hardware raw ID; high variant bits are ignored. */
AT_SOC_KIND
AtSocKindFromRawChipId (
  IN UINT32 RawChipId
  );

CONST CHAR16 *
AtSocKindName (
  IN AT_SOC_KIND Kind
  );

/** Locate ChipInfo and collect the raw ID and printable chip name. */
EFI_STATUS
AtSocDetect (
  OUT AT_SOC_INFO *Info
  );

#endif /* __ANDROID_TOOLS_SOC_INFO_H__ */
