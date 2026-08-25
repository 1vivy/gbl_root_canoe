/*
 * USB mass-storage SCSI command decoder.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbMscScsi.h"

STATIC
UINT32
SfbMscBe32 (IN CONST UINT8 *Bytes)
{
  return ((UINT32)Bytes[0] << 24) | ((UINT32)Bytes[1] << 16) |
         ((UINT32)Bytes[2] << 8) | (UINT32)Bytes[3];
}


STATIC
VOID
SfbMscPutBe32 (OUT UINT8 *Bytes, IN UINT32 Value)
{
  Bytes[0] = (UINT8)(Value >> 24);
  Bytes[1] = (UINT8)(Value >> 16);
  Bytes[2] = (UINT8)(Value >> 8);
  Bytes[3] = (UINT8)Value;
}

STATIC
VOID
SfbMscPutBe64 (OUT UINT8 *Bytes, IN UINT64 Value)
{
  UINTN  Index;

  for (Index = 0; Index < 8; Index++) {
    Bytes[7 - Index] = (UINT8)Value;
    Value >>= 8;
  }
}

STATIC
UINT32
SfbMscMin32 (IN UINT32 A, IN UINT32 B)
{
  return (A < B) ? A : B;
}

VOID
SfbMscScsiSetSense (
  IN OUT SFB_MSC_SENSE *Sense,
  IN UINT8              SenseKey,
  IN UINT8              Asc,
  IN UINT8              Ascq
  )
{
  if (Sense == NULL) {
    return;
  }
  Sense->SenseKey = SenseKey;
  Sense->AdditionalSenseCode = Asc;
  Sense->AdditionalSenseCodeQualifier = Ascq;
  Sense->Valid = TRUE;
}

STATIC
VOID
SfbMscCheckCondition (
  IN OUT SFB_MSC_SENSE         *Sense,
  IN UINT8                      Key,
  IN UINT8                      Asc,
  IN OUT SFB_MSC_SCSI_RESPONSE *Response
  )
{
  SfbMscScsiSetSense (Sense, Key, Asc, 0);
  Response->ScsiStatus = SFB_MSC_SCSI_CHECK_CONDITION;
}

STATIC
BOOLEAN
SfbMscMediaReady (IN CONST SFB_MSC_LUN *Lun)
{
  return (BOOLEAN)(Lun != NULL && Lun->BlockIo != NULL &&
                   Lun->BlockIo->Media != NULL &&
                   Lun->BlockIo->Media->MediaPresent &&
                   Lun->BlockIo->Media->BlockSize != 0);
}

STATIC
VOID
SfbMscCopyAscii (OUT UINT8 *Destination, IN UINTN Length, IN CONST CHAR8 *Source)
{
  UINTN  Index;

  for (Index = 0; Index < Length; Index++) {
    Destination[Index] = (Source == NULL) ? ' ' : (UINT8)Source[Index];
    if (Source != NULL && Source[Index] == '\0') {
      Destination[Index] = ' ';
    }
  }
}

STATIC
BOOLEAN
SfbMscPrepareRw (
  IN CONST UINT8              *Cdb,
  IN UINTN                     CdbLength,
  IN BOOLEAN                   Write,
  IN CONST SFB_MSC_LUN        *Lun,
  IN OUT SFB_MSC_SENSE        *Sense,
  IN OUT SFB_MSC_SCSI_RESPONSE *Response
  )
{
  UINT64  Lba;
  UINT32  Blocks;
  UINT32  BlockSize;

  if ((Cdb[0] == 0xA8 || Cdb[0] == 0xAA) ?
      CdbLength < 12 : CdbLength < 10) {
    SfbMscCheckCondition (Sense, SFB_MSC_SENSE_ILLEGAL_REQUEST,
                          SFB_MSC_ASC_INVALID_COMMAND, Response);
    return FALSE;
  }

  if (!SfbMscMediaReady (Lun)) {
    SfbMscCheckCondition (Sense, SFB_MSC_SENSE_NOT_READY,
                          SFB_MSC_ASC_MEDIUM_NOT_PRESENT, Response);
    return FALSE;
  }

  if (Write && Lun->ReadOnly) {
    SfbMscCheckCondition (Sense, SFB_MSC_SENSE_DATA_PROTECT,
                          SFB_MSC_ASC_WRITE_PROTECTED, Response);
    return FALSE;
  }

  Lba = SfbMscBe32 (Cdb + 2);
  if (Cdb[0] == 0xA8 || Cdb[0] == 0xAA) {
    Blocks = SfbMscBe32 (Cdb + 6);
  } else {
    Blocks = ((UINT32)Cdb[7] << 8) | Cdb[8];
    if (Blocks == 0) {
      Blocks = 0x10000;
    }
    }

  BlockSize = Lun->BlockIo->Media->BlockSize;
  if (Blocks != 0 && (Lba > Lun->BlockIo->Media->LastBlock ||
      (UINT64)(Blocks - 1) > Lun->BlockIo->Media->LastBlock - Lba)) {
    SfbMscCheckCondition (Sense, SFB_MSC_SENSE_ILLEGAL_REQUEST,
                          SFB_MSC_ASC_LBA_OUT_OF_RANGE, Response);
    return FALSE;
  }

  if (Blocks != 0 && Blocks > MAX_UINT32 / BlockSize) {
    SfbMscCheckCondition (Sense, SFB_MSC_SENSE_ILLEGAL_REQUEST,
                          SFB_MSC_ASC_LBA_OUT_OF_RANGE, Response);
    return FALSE;
  }

  Response->DataDirection = Write ? SFB_MSC_SCSI_DIR_OUT : SFB_MSC_SCSI_DIR_IN;
  Response->Lba = Lba;
  Response->Blocks = Blocks;
  Response->BlockSize = BlockSize;
  Response->DataLength = Blocks * BlockSize;
  return TRUE;
}

STATIC
VOID
SfbMscBuildRequestSense (
  IN OUT SFB_MSC_SENSE         *Sense,
  IN UINT8                      AllocationLength,
  IN OUT SFB_MSC_SCSI_RESPONSE *Response
  )
{
  UINT32  Length;
  UINT8   Key = SFB_MSC_SENSE_NO_SENSE;
  UINT8   Asc = 0;
  UINT8   Ascq = 0;

  if (Sense != NULL && Sense->Valid) {
    Key = Sense->SenseKey;
    Asc = Sense->AdditionalSenseCode;
    Ascq = Sense->AdditionalSenseCodeQualifier;
  }

  Response->Data[0] = 0x70;
  Response->Data[2] = Key;
  Response->Data[7] = 10;
  Response->Data[12] = Asc;
  Response->Data[13] = Ascq;
  Length = SfbMscMin32 (18, AllocationLength);
  Response->DataLength = Length;
  Response->DataDirection = SFB_MSC_SCSI_DIR_IN;
  Sense->Valid = FALSE;
  Sense->SenseKey = SFB_MSC_SENSE_NO_SENSE;
  Sense->AdditionalSenseCode = 0;
  Sense->AdditionalSenseCodeQualifier = 0;
}

EFI_STATUS
SfbMscScsiCommand (
  IN CONST UINT8             *Cdb,
  IN UINTN                    CdbLength,
  IN CONST SFB_MSC_LUN       *Luns,
  IN UINTN                    LunCount,
  IN UINT8                    Lun,
  IN OUT SFB_MSC_SENSE       *Sense,
  OUT SFB_MSC_SCSI_RESPONSE  *Response
  )
{
  CONST SFB_MSC_LUN  *ThisLun;
  UINT8               Opcode;
  UINT32              Allocation;
  UINT64              LastBlock;
  UINTN               Index;

  if (Cdb == NULL || Response == NULL || Sense == NULL ||
      CdbLength == 0 || CdbLength > SFB_MSC_SCSI_CDB_BYTES ||
      Luns == NULL || LunCount == 0 || LunCount > SFB_MSC_MAX_LUNS) {
    return EFI_INVALID_PARAMETER;
  }

  for (Index = 0; Index < sizeof (*Response); Index++) {
    ((UINT8 *)Response)[Index] = 0;
  }
  Opcode = Cdb[0];
  if (Lun >= LunCount) {
    SfbMscCheckCondition (Sense, SFB_MSC_SENSE_ILLEGAL_REQUEST,
                          SFB_MSC_ASC_INVALID_COMMAND, Response);
    return EFI_SUCCESS;
  }
  ThisLun = &Luns[Lun];

  if (Opcode == 0x03) {
    if (CdbLength < 6) {
      SfbMscCheckCondition (Sense, SFB_MSC_SENSE_ILLEGAL_REQUEST,
                            SFB_MSC_ASC_INVALID_COMMAND, Response);
      return EFI_SUCCESS;
    }
    SfbMscBuildRequestSense (Sense, Cdb[4], Response);
    return EFI_SUCCESS;
  }

  switch (Opcode) {
  case 0x00: /* TEST UNIT READY */
    if (!SfbMscMediaReady (ThisLun)) {
      SfbMscCheckCondition (Sense, SFB_MSC_SENSE_NOT_READY,
                            SFB_MSC_ASC_MEDIUM_NOT_PRESENT, Response);
    }
    break;

  case 0x12: /* INQUIRY */
    if (CdbLength < 6 || (Cdb[1] & 1) != 0 || Cdb[2] != 0) {
      SfbMscCheckCondition (Sense, SFB_MSC_SENSE_ILLEGAL_REQUEST,
                            SFB_MSC_ASC_INVALID_COMMAND, Response);
      break;
    }
    Response->Data[2] = 5;
    Response->Data[3] = 2;
    Response->Data[4] = 31;
    SfbMscCopyAscii (Response->Data + 8, 8, ThisLun->Vendor);
    SfbMscCopyAscii (Response->Data + 16, 16, ThisLun->Product);
    Response->DataDirection = SFB_MSC_SCSI_DIR_IN;
    Response->DataLength = SfbMscMin32 (36, Cdb[4]);
    break;

  case 0x25: /* READ CAPACITY (10) */
    if (!SfbMscMediaReady (ThisLun)) {
      SfbMscCheckCondition (Sense, SFB_MSC_SENSE_NOT_READY,
                            SFB_MSC_ASC_MEDIUM_NOT_PRESENT, Response);
      break;
    }
    LastBlock = ThisLun->BlockIo->Media->LastBlock;
    SfbMscPutBe32 (Response->Data,
                   (LastBlock > MAX_UINT32) ? MAX_UINT32 : (UINT32)LastBlock);
    SfbMscPutBe32 (Response->Data + 4,
                   ThisLun->BlockIo->Media->BlockSize);
    Response->DataDirection = SFB_MSC_SCSI_DIR_IN;
    Response->DataLength = 8;
    break;

  case 0x9E: /* SERVICE ACTION IN (16), READ CAPACITY (16) */
    if (CdbLength < 16 || (Cdb[1] & 0x1F) != 0x10) {
      SfbMscCheckCondition (Sense, SFB_MSC_SENSE_ILLEGAL_REQUEST,
                            SFB_MSC_ASC_INVALID_COMMAND, Response);
      break;
    }
    if (!SfbMscMediaReady (ThisLun)) {
      SfbMscCheckCondition (Sense, SFB_MSC_SENSE_NOT_READY,
                            SFB_MSC_ASC_MEDIUM_NOT_PRESENT, Response);
      break;
    }
    Allocation = SfbMscBe32 (Cdb + 10);
    LastBlock = ThisLun->BlockIo->Media->LastBlock;
    SfbMscPutBe64 (Response->Data, LastBlock);
    SfbMscPutBe32 (Response->Data + 8,
                   ThisLun->BlockIo->Media->BlockSize);
    Response->DataDirection = SFB_MSC_SCSI_DIR_IN;
    Response->DataLength = SfbMscMin32 (32, Allocation);
    break;

  case 0x28: /* READ (10) */
    SfbMscPrepareRw (Cdb, CdbLength, FALSE, ThisLun, Sense, Response);
    break;

  case 0x2A: /* WRITE (10) */
    SfbMscPrepareRw (Cdb, CdbLength, TRUE, ThisLun, Sense, Response);
    break;

  case 0xA8: /* READ (12) */
    SfbMscPrepareRw (Cdb, CdbLength, FALSE, ThisLun, Sense, Response);
    break;

  case 0xAA: /* WRITE (12) */
    SfbMscPrepareRw (Cdb, CdbLength, TRUE, ThisLun, Sense, Response);
    break;

  case 0x1A: /* MODE SENSE (6) */
    if (CdbLength < 6 || (((Cdb[2] & 0x3F) != 0x00) &&
                         ((Cdb[2] & 0x3F) != 0x3F))) {
      SfbMscCheckCondition (Sense, SFB_MSC_SENSE_ILLEGAL_REQUEST,
                            SFB_MSC_ASC_INVALID_COMMAND, Response);
      break;
    }
    Response->Data[0] = 3;
    Response->Data[2] = ThisLun->ReadOnly ? 0x80 : 0;
    Response->DataDirection = SFB_MSC_SCSI_DIR_IN;
    Response->DataLength = SfbMscMin32 (4, Cdb[4]);
    break;

  case 0x5A: /* MODE SENSE (10) */
    if (CdbLength < 10 || (((Cdb[2] & 0x3F) != 0x00) &&
                          ((Cdb[2] & 0x3F) != 0x3F))) {
      SfbMscCheckCondition (Sense, SFB_MSC_SENSE_ILLEGAL_REQUEST,
                            SFB_MSC_ASC_INVALID_COMMAND, Response);
      break;
    }
    Response->Data[1] = 6;
    Response->Data[2] = ThisLun->ReadOnly ? 0x80 : 0;
    Response->DataDirection = SFB_MSC_SCSI_DIR_IN;
    Allocation = ((UINT32)Cdb[7] << 8) | Cdb[8];
    Response->DataLength = SfbMscMin32 (8, Allocation);
    break;

  case 0x1E: /* PREVENT/ALLOW MEDIUM REMOVAL */
    break;

  case 0x35: /* SYNCHRONIZE CACHE (10) */
    Response->Flush = TRUE;
    break;

  case 0x1B: /* START STOP UNIT */
    if (CdbLength < 6) {
      SfbMscCheckCondition (Sense, SFB_MSC_SENSE_ILLEGAL_REQUEST,
                            SFB_MSC_ASC_INVALID_COMMAND, Response);
      break;
    }
    Response->Eject = (BOOLEAN)((Cdb[4] & 2) != 0);
    break;

  case 0xA0: /* REPORT LUNS */
    if (CdbLength < 12) {
      SfbMscCheckCondition (Sense, SFB_MSC_SENSE_ILLEGAL_REQUEST,
                            SFB_MSC_ASC_INVALID_COMMAND, Response);
      break;
    }
    Allocation = SfbMscBe32 (Cdb + 6);
    SfbMscPutBe32 (Response->Data, (UINT32)(LunCount * 8));
    for (Index = 0; Index < LunCount; Index++) {
      Response->Data[8 + Index * 8] = (UINT8)Index;
    }
    Response->DataDirection = SFB_MSC_SCSI_DIR_IN;
    Response->DataLength = SfbMscMin32 ((UINT32)(8 + LunCount * 8), Allocation);
    break;

  default:
    SfbMscCheckCondition (Sense, SFB_MSC_SENSE_ILLEGAL_REQUEST,
                          SFB_MSC_ASC_INVALID_COMMAND, Response);
    break;
  }

  return EFI_SUCCESS;
}
