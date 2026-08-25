/*
 * USB mass-storage Bulk-Only Transport target.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SUPER_FB_MSC_H__
#define __SUPER_FB_MSC_H__

#include "SuperFbMscScsi.h"

#define SFB_MSC_CBW_SIGNATURE  0x43425355U
#define SFB_MSC_CSW_SIGNATURE  0x53425355U
#define SFB_MSC_CBW_BYTES      31
#define SFB_MSC_CSW_BYTES      13
#define SFB_MSC_TRANSFER_BYTES (64 * 1024)

#pragma pack(1)
typedef struct {
  UINT32 Signature;
  UINT32 Tag;
  UINT32 DataTransferLength;
  UINT8  Flags;
  UINT8  Lun;
  UINT8  CdbLength;
  UINT8  Cdb[16];
} SFB_MSC_CBW;

typedef struct {
  UINT32 Signature;
  UINT32 Tag;
  UINT32 DataResidue;
  UINT8  Status;
} SFB_MSC_CSW;
#pragma pack()

/* The CSW reaches the wire as a byte copy of this struct, so its packed layout
 * IS the protocol. Assert it here rather than discover it as a host-side
 * "invalid CSW" that only reproduces on a device. */
STATIC_ASSERT (sizeof (SFB_MSC_CSW) == SFB_MSC_CSW_BYTES,
               "BOT command status wrapper must be exactly 13 bytes");

EFI_STATUS
SfbMscParseCbw (
  IN CONST VOID *Buffer,
  IN UINTN       BufferBytes,
  OUT SFB_MSC_CBW *Cbw,
  IN UINTN       LunCount
  );

VOID
SfbMscBuildCsw (
  OUT SFB_MSC_CSW *Csw,
  IN UINT32        Tag,
  IN UINT32        Residue,
  IN UINT8         Status
  );

EFI_STATUS
SfbMscRun (IN CONST SFB_MSC_LUN *Luns, IN UINTN LunCount);
VOID
SfbUsbCensus (VOID);


#endif
