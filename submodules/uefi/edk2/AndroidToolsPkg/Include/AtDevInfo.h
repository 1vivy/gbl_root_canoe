/** @file
 *  AndroidTools accessors for the shared Verified Boot DeviceInfo blob.
 *
 *  The byte-exact constants and layout are canonical in
 *  QcomModulePkg/Include/Library/DeviceInfo.h.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __AT_DEV_INFO_H__
#define __AT_DEV_INFO_H__

#include <Uefi.h>
#include <Library/DeviceInfo.h>

/**
  Read the whole DeviceInfo blob from the persist partition via the Verified
  Boot protocol. Returns EFI_SUCCESS and fills DevInfo.
**/
EFI_STATUS
AtDevInfoRead (
  OUT DeviceInfo *DevInfo
  );

/**
  Write the whole DeviceInfo blob back to the persist partition.
**/
EFI_STATUS
AtDevInfoWrite (
  IN CONST DeviceInfo *DevInfo
  );

/**
  Read one rollback-index slot. Loc is 0..MAX_VB_PARTITIONS-1. DevInfo must
  have been loaded first (via AtDevInfoRead).
**/
EFI_STATUS
AtReadRollbackIndex (
  IN  CONST DeviceInfo *DevInfo,
  IN  UINT32            Loc,
  OUT UINT64            *RollbackIndex
  );

/**
  Zero every rollback-index slot in DevInfo in memory. Caller persists with
  AtDevInfoWrite.
**/
VOID
AtClearRollbackIndex (
  IN OUT DeviceInfo *DevInfo
  );

#endif /* __AT_DEV_INFO_H__ */
