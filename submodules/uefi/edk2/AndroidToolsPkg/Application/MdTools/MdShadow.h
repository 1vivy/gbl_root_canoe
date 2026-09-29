/** @file
  Target model for one-entry minidump shadow registrations.

  A target copies one live AOP/BOOT region descriptor under a unique Canoe
  alias. The payload stays where firmware placed it; only the one-entry region
  array lives in MdTools memory, so the registration is valid only while the
  image remains resident.

  Copyright (c) 2026, contributors to the canoe ABL tree.
  SPDX-License-Identifier: BSD-3-Clause
**/
#ifndef __MD_SHADOW_H__
#define __MD_SHADOW_H__

#include <Uefi.h>
#include <MdTable.h>

#define MD_SHADOW_MAX_TARGETS \
  (MD_MAX_ARRAYS * MD_MAX_APPEND_REGIONS)
#define MD_SHADOW_LABEL_CHARS 80u

typedef struct {
  UINTN           ArrayIndex;
  UINTN           EntryIndex;
  UINTN           SubsystemIndex;
  UINT32          EncryptionRequired;
  MD_REGION_ENTRY Source;
  CHAR8           Alias[MD_REGION_NAME_LEN];
} MD_SHADOW_TARGET;

/**
  Collect every validated AOP/BOOT region in bounded array order. Capacity is
  measured in MD_SHADOW_TARGET elements. On EFI_BUFFER_TOO_SMALL, Count returns
  the required capacity and Targets remains untouched.
**/
EFI_STATUS
MdShadowCollectTargets (
  IN  CONST MD_TABLE_MAP *Map,
  OUT MD_SHADOW_TARGET   *Targets,
  IN  UINTN               Capacity,
  OUT UINTN              *Count
  );

/** Build the one-entry alias without copying or changing the source payload. **/
EFI_STATUS
MdShadowBuildRegion (
  IN  CONST MD_SHADOW_TARGET *Target,
  OUT MD_REGION_ENTRY        *Region
  );

/** Show the target picker and run at most one resident shadow experiment. **/
VOID
MdRunShadowTargetMenu (
  VOID
  );

#endif /* __MD_SHADOW_H__ */
