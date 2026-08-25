/*
 * USB mass-storage SCSI command decoding and response construction.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SUPER_FB_MSC_SCSI_H__
#define __SUPER_FB_MSC_SCSI_H__

#include <Uefi.h>
#include <Protocol/BlockIo.h>

#define SFB_MSC_MAX_LUNS             8
#define SFB_MSC_SCSI_CDB_BYTES       16
#define SFB_MSC_SCSI_INLINE_BYTES    256

#define SFB_MSC_SCSI_GOOD            0x00
#define SFB_MSC_SCSI_CHECK_CONDITION 0x02

#define SFB_MSC_SENSE_NO_SENSE       0x00
#define SFB_MSC_SENSE_NOT_READY      0x02
#define SFB_MSC_SENSE_MEDIUM_ERROR   0x03
#define SFB_MSC_SENSE_ILLEGAL_REQUEST 0x05
#define SFB_MSC_SENSE_UNIT_ATTENTION 0x06
#define SFB_MSC_SENSE_DATA_PROTECT   0x07

#define SFB_MSC_ASC_INVALID_COMMAND  0x20
#define SFB_MSC_ASC_LBA_OUT_OF_RANGE 0x21
#define SFB_MSC_ASC_WRITE_PROTECTED  0x27
#define SFB_MSC_ASC_MEDIUM_NOT_PRESENT 0x3A

#define SFB_MSC_SCSI_DIR_NONE        0
#define SFB_MSC_SCSI_DIR_IN         1
#define SFB_MSC_SCSI_DIR_OUT        2

typedef struct {
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  BOOLEAN                ReadOnly;
  CHAR8                  Vendor[8];
  CHAR8                  Product[16];
} SFB_MSC_LUN;

typedef struct {
  UINT8 SenseKey;
  UINT8 AdditionalSenseCode;
  UINT8 AdditionalSenseCodeQualifier;
  BOOLEAN Valid;
} SFB_MSC_SENSE;

typedef struct {
  UINT8  ScsiStatus;
  UINT8  DataDirection;
  UINT64 Lba;
  UINT32 Blocks;
  UINT32 BlockSize;
  UINT32 DataLength;
  BOOLEAN Eject;
  BOOLEAN Flush;
  UINT8  Data[SFB_MSC_SCSI_INLINE_BYTES];
} SFB_MSC_SCSI_RESPONSE;

/*
 * Decode one CDB and construct its response without touching the media.  READ
 * and WRITE return Lba/Blocks/BlockSize metadata for the BOT engine to execute;
 * all small command responses are placed in Data.  Sense is stateful by design:
 * a failing command records sense and REQUEST SENSE reports then clears it.
 */
EFI_STATUS
SfbMscScsiCommand (
  IN CONST UINT8          *Cdb,
  IN UINTN                  CdbLength,
  IN CONST SFB_MSC_LUN    *Luns,
  IN UINTN                  LunCount,
  IN UINT8                  Lun,
  IN OUT SFB_MSC_SENSE    *Sense,
  OUT SFB_MSC_SCSI_RESPONSE *Response
  );

VOID
SfbMscScsiSetSense (
  IN OUT SFB_MSC_SENSE *Sense,
  IN UINT8              SenseKey,
  IN UINT8              Asc,
  IN UINT8              Ascq
  );

#endif
