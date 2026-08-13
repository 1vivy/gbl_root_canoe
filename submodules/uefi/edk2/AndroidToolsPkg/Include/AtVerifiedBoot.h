/** @file
 *  Minimal port of the Qualcomm Verified Boot protocol definition, trimmed to
 *  the one member AndroidToolsPkg needs: VBRwDeviceState, which reads/writes
 *  the raw DeviceInfo blob backed by the persist partition.
 *
 *  Only the first two fields (Revision, VBRwDeviceState) are declared because
 *  that is all callers touch; the platform-produced instance has more members
 *  laid out after them, which is irrelevant to a pointer that only dereferences
 *  these two.
 *
 *  Source: edk2-uefi.lnx.6.0.r32 QcomModulePkg/Include/Protocol/EFIVerifiedBoot.h
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __AT_VERIFIED_BOOT_H__
#define __AT_VERIFIED_BOOT_H__

#include <Uefi.h>

/** Device-state operations used by VBRwDeviceState. */
typedef enum {
  READ_CONFIG,
  WRITE_CONFIG,
  DEVICE_STATE_MAX = (int)0xFFFFFFFFULL
} vb_device_state_op_t;

typedef EFI_STATUS (EFIAPI *QCOM_VB_RW_DEVICE_STATE) (
    IN VOID   *This,
    IN vb_device_state_op_t Op,
    IN OUT UINT8 *Buf,
    IN UINT32 BufLen);

typedef struct _QCOM_VERIFIEDBOOT_PROTOCOL {
  UINT64 Revision;
  QCOM_VB_RW_DEVICE_STATE VBRwDeviceState;
  /* Further members exist on the platform instance but are unused here. */
} QCOM_VERIFIEDBOOT_PROTOCOL;

extern EFI_GUID gEfiQcomVerifiedBootProtocolGuid;

#endif /* __AT_VERIFIED_BOOT_H__ */
