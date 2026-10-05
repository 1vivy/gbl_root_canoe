/** @file
 *  Qualcomm ChipInfo protocol-backed SoC detection.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/AndroidToolsSocInfo.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>

EFI_STATUS
AtSocDetect (
  OUT AT_SOC_INFO *Info
  )
{
  EFI_CHIPINFO_PROTOCOL *ChipInfo;

  if (Info == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (Info, sizeof (*Info));
  Info->LocateStatus = gBS->LocateProtocol (
    &gEfiChipInfoProtocolGuid,
    NULL,
    (VOID **)&ChipInfo
    );
  if (EFI_ERROR (Info->LocateStatus) || ChipInfo == NULL) {
    Info->RawIdStatus = EFI_NOT_FOUND;
    Info->NameStatus = EFI_NOT_FOUND;
    return EFI_ERROR (Info->LocateStatus) ? Info->LocateStatus : EFI_NOT_FOUND;
  }
  Info->ProtocolRevision = ChipInfo->Revision;
  if (ChipInfo->GetRawChipId == NULL) {
    Info->RawIdStatus = EFI_UNSUPPORTED;
  } else {
    Info->RawIdStatus = ChipInfo->GetRawChipId (ChipInfo, &Info->RawChipId);
  }
  if (ChipInfo->GetChipIdString == NULL) {
    Info->NameStatus = EFI_UNSUPPORTED;
  } else {
    Info->NameStatus = ChipInfo->GetChipIdString (
      ChipInfo,
      Info->ChipIdString,
      sizeof (Info->ChipIdString)
      );
    Info->ChipIdString[sizeof (Info->ChipIdString) - 1] = '\0';
  }
  if (EFI_ERROR (Info->RawIdStatus)) {
    Info->Kind = AtSocUnknown;
    return Info->RawIdStatus;
  }
  Info->Kind = AtSocKindFromRawChipId (Info->RawChipId);
  return (Info->Kind == AtSocUnknown) ? EFI_UNSUPPORTED : EFI_SUCCESS;
}
