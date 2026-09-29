/** @file
  Bounded model for interactive minidump shadow targets.

  Copyright (c) 2026, contributors to the canoe ABL tree.
  SPDX-License-Identifier: BSD-3-Clause
**/
#include <Uefi.h>

#include <Library/BaseMemoryLib.h>

#include "MdShadow.h"

STATIC
EFI_STATUS
MdShadowSubsystemCode (
  IN  UINTN  SubsystemIndex,
  OUT CHAR8 *Code
  )
{
  if (Code == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (SubsystemIndex == MD_SS_AOP) {
    *Code = 'A';
    return EFI_SUCCESS;
  }
  if (SubsystemIndex == MD_SS_BOOT) {
    *Code = 'B';
    return EFI_SUCCESS;
  }
  return EFI_COMPROMISED_DATA;
}

STATIC
VOID
MdShadowBuildAlias (
  OUT CHAR8 Alias[MD_REGION_NAME_LEN],
  IN  CHAR8 Code,
  IN  UINTN EntryIndex
  )
{
  ZeroMem (Alias, MD_REGION_NAME_LEN);
  CopyMem (Alias, "CANOE-", 6);
  Alias[6] = Code;
  Alias[7] = (CHAR8)('0' + (EntryIndex / 10));
  Alias[8] = (CHAR8)('0' + (EntryIndex % 10));
}

EFI_STATUS
MdShadowCollectTargets (
  IN  CONST MD_TABLE_MAP *Map,
  OUT MD_SHADOW_TARGET   *Targets,
  IN  UINTN               Capacity,
  OUT UINTN              *Count
  )
{
  EFI_STATUS Status;
  UINTN      ArrayIndex;
  UINTN      EntryIndex;
  UINTN      Required;
  CHAR8      Code;

  if (Map == NULL || Targets == NULL || Count == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Count = 0;
  if (Map->ArrayCount > MD_MAX_ARRAYS) {
    return EFI_COMPROMISED_DATA;
  }

  Required = 0;
  for (ArrayIndex = 0; ArrayIndex < Map->ArrayCount; ++ArrayIndex) {
    Status = MdShadowSubsystemCode (
               Map->Arrays[ArrayIndex].SubsystemIndex,
               &Code
               );
    if (EFI_ERROR (Status) ||
        Map->Arrays[ArrayIndex].Count > MD_MAX_APPEND_REGIONS) {
      return EFI_COMPROMISED_DATA;
    }
    Required += Map->Arrays[ArrayIndex].Count;
  }
  if (Required > MD_SHADOW_MAX_TARGETS) {
    return EFI_COMPROMISED_DATA;
  }
  if (Capacity < Required) {
    *Count = Required;
    return EFI_BUFFER_TOO_SMALL;
  }

  for (ArrayIndex = 0; ArrayIndex < Map->ArrayCount; ++ArrayIndex) {
    Status = MdShadowSubsystemCode (
               Map->Arrays[ArrayIndex].SubsystemIndex,
               &Code
               );
    if (EFI_ERROR (Status)) {
      return Status;
    }
    for (EntryIndex = 0;
         EntryIndex < Map->Arrays[ArrayIndex].Count;
         ++EntryIndex) {
      ZeroMem (&Targets[*Count], sizeof (Targets[*Count]));
      Status = MdTableReadEntry (
                 Map,
                 ArrayIndex,
                 EntryIndex,
                 &Targets[*Count].Source
                 );
      if (EFI_ERROR (Status)) {
        *Count = 0;
        return Status;
      }
      Targets[*Count].ArrayIndex = ArrayIndex;
      Targets[*Count].EntryIndex = EntryIndex;
      Targets[*Count].SubsystemIndex =
        Map->Arrays[ArrayIndex].SubsystemIndex;
      Targets[*Count].EncryptionRequired =
        Map->Arrays[ArrayIndex].EncryptionRequired;
      MdShadowBuildAlias (Targets[*Count].Alias, Code, EntryIndex);
      ++*Count;
    }
  }
  return EFI_SUCCESS;
}

EFI_STATUS
MdShadowBuildRegion (
  IN  CONST MD_SHADOW_TARGET *Target,
  OUT MD_REGION_ENTRY        *Region
  )
{
  if (Target == NULL || Region == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (Target->Alias[0] == '\0' ||
      Target->Source.Valid != MD_REGION_VALID_VALUE ||
      Target->Source.Address < MD_MIN_REGION_ADDRESS ||
      Target->Source.Address >= MD_MAX_REGION_ADDRESS ||
      Target->Source.Size == 0 ||
      Target->Source.Size > MD_MAX_REGION_SIZE) {
    return EFI_COMPROMISED_DATA;
  }

  ZeroMem (Region, sizeof (*Region));
  CopyMem (Region->Name, Target->Alias, MD_REGION_NAME_LEN);
  Region->SeqNum = 0;
  Region->Valid = MD_REGION_VALID_VALUE;
  Region->Address = Target->Source.Address;
  Region->Size = Target->Source.Size;
  return EFI_SUCCESS;
}
