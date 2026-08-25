/* libc headers must precede every EDK2 header; see test_fat.c. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#undef NULL

#include <Uefi.h>
#include <Protocol/BlockIo.h>

#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbMsc.h"

static EFI_BLOCK_IO_MEDIA Media = {
  1, FALSE, TRUE, TRUE, FALSE, FALSE, 512, 1, 31, 0, 0, 0
};
static EFI_BLOCK_IO_PROTOCOL BlockIo = {
  EFI_BLOCK_IO_PROTOCOL_REVISION, &Media, NULL, NULL, NULL, NULL
};
static SFB_MSC_LUN Lun;

static void
SetBe32(UINT8 *Bytes, UINT32 Value)
{
  Bytes[0] = (UINT8)(Value >> 24);
  Bytes[1] = (UINT8)(Value >> 16);
  Bytes[2] = (UINT8)(Value >> 8);
  Bytes[3] = (UINT8)Value;
}

static void
SetLe32(UINT8 *Bytes, UINT32 Value)
{
  Bytes[0] = (UINT8)Value;
  Bytes[1] = (UINT8)(Value >> 8);
  Bytes[2] = (UINT8)(Value >> 16);
  Bytes[3] = (UINT8)(Value >> 24);
}

static EFI_STATUS
Command(const UINT8 *Cdb, UINTN Length, SFB_MSC_SENSE *Sense,
        SFB_MSC_SCSI_RESPONSE *Response)
{
  return SfbMscScsiCommand (Cdb, Length, &Lun, 1, 0, Sense, Response);
}

static void
TestCbwCswFraming(void)
{
  UINT8 Raw[SFB_MSC_CBW_BYTES];
  SFB_MSC_CBW Cbw;
  SFB_MSC_CSW Csw;

  memset (Raw, 0, sizeof (Raw));
  SetLe32 (Raw, SFB_MSC_CBW_SIGNATURE);
  SetLe32 (Raw + 4, 0xAABBCCDD);
  SetLe32 (Raw + 8, 36);
  Raw[12] = 0x80;
  Raw[13] = 0;
  Raw[14] = 6;
  Raw[15] = 0x12;
  assert (SfbMscParseCbw (Raw, sizeof (Raw), &Cbw, 1) == EFI_SUCCESS);
  assert (Cbw.Tag == 0xAABBCCDD && Cbw.DataTransferLength == 36);
  assert (Cbw.Flags == 0x80 && Cbw.CdbLength == 6 && Cbw.Cdb[0] == 0x12);

  Raw[0] ^= 1;
  assert (SfbMscParseCbw (Raw, sizeof (Raw), &Cbw, 1) == EFI_COMPROMISED_DATA);
  Raw[0] ^= 1;
  Raw[12] = 0x01;
  assert (SfbMscParseCbw (Raw, sizeof (Raw), &Cbw, 1) == EFI_COMPROMISED_DATA);

  SfbMscBuildCsw (&Csw, 0xAABBCCDD, 7, 1);
  assert (Csw.Signature == SFB_MSC_CSW_SIGNATURE);
  assert (Csw.Tag == 0xAABBCCDD && Csw.DataResidue == 7 && Csw.Status == 1);
}

static void
TestInquiryAndCapacity(void)
{
  UINT8 Cdb[16] = { 0 };
  SFB_MSC_SENSE Sense = { 0 };
  SFB_MSC_SCSI_RESPONSE Response;

  Cdb[0] = 0x12;
  Cdb[4] = 36;
  assert (Command (Cdb, 6, &Sense, &Response) == EFI_SUCCESS);
  assert (Response.ScsiStatus == SFB_MSC_SCSI_GOOD);
  assert (Response.DataLength == 36 && Response.Data[2] == 5);
  assert (memcmp (Response.Data + 8, "CANOE   ", 8) == 0);
  assert (memcmp (Response.Data + 16, "persist         ", 16) == 0);

  memset (Cdb, 0, sizeof (Cdb));
  Cdb[0] = 0x25;
  assert (Command (Cdb, 10, &Sense, &Response) == EFI_SUCCESS);
  assert (Response.DataLength == 8);
  assert (Response.Data[0] == 0 && Response.Data[1] == 0 &&
          Response.Data[2] == 0 && Response.Data[3] == 31);
  assert (Response.Data[4] == 0 && Response.Data[5] == 0 &&
          Response.Data[6] == 2 && Response.Data[7] == 0);

  memset (Cdb, 0, sizeof (Cdb));
  Cdb[0] = 0x9E;
  Cdb[1] = 0x10;
  Cdb[13] = 32;
  assert (Command (Cdb, 16, &Sense, &Response) == EFI_SUCCESS);
  assert (Response.DataLength == 32 && Response.Data[7] == 31);
}

static void
TestReadWriteBoundsAndSense(void)
{
  UINT8 Cdb[16] = { 0 };
  SFB_MSC_SENSE Sense = { 0 };
  SFB_MSC_SCSI_RESPONSE Response;

  Cdb[0] = 0x28;
  Cdb[8] = 1;
  Cdb[5] = 31;
  assert (Command (Cdb, 10, &Sense, &Response) == EFI_SUCCESS);
  assert (Response.ScsiStatus == SFB_MSC_SCSI_GOOD);
  assert (Response.Lba == 31 && Response.Blocks == 1);

  Cdb[5] = 32;
  assert (Command (Cdb, 10, &Sense, &Response) == EFI_SUCCESS);
  assert (Response.ScsiStatus == SFB_MSC_SCSI_CHECK_CONDITION);
  assert (Sense.Valid && Sense.SenseKey == SFB_MSC_SENSE_ILLEGAL_REQUEST &&
          Sense.AdditionalSenseCode == SFB_MSC_ASC_LBA_OUT_OF_RANGE);
  memset (Cdb, 0, sizeof (Cdb));
  Cdb[0] = 0x2A;
  Cdb[5] = 31;
  Cdb[8] = 2;
  assert (Command (Cdb, 10, &Sense, &Response) == EFI_SUCCESS);
  assert (Response.ScsiStatus == SFB_MSC_SCSI_CHECK_CONDITION &&
          Sense.AdditionalSenseCode == SFB_MSC_ASC_LBA_OUT_OF_RANGE);

  memset (Cdb, 0, sizeof (Cdb));
  Cdb[0] = 0xA8;
  SetBe32 (Cdb + 2, 31);
  SetBe32 (Cdb + 6, 2);
  assert (Command (Cdb, 12, &Sense, &Response) == EFI_SUCCESS);
  assert (Response.ScsiStatus == SFB_MSC_SCSI_CHECK_CONDITION &&
          Sense.AdditionalSenseCode == SFB_MSC_ASC_LBA_OUT_OF_RANGE);

  Lun.ReadOnly = TRUE;
  memset (Cdb, 0, sizeof (Cdb));
  Cdb[0] = 0x2A;
  Cdb[8] = 1;
  assert (Command (Cdb, 10, &Sense, &Response) == EFI_SUCCESS);
  assert (Response.ScsiStatus == SFB_MSC_SCSI_CHECK_CONDITION);
  assert (Sense.SenseKey == SFB_MSC_SENSE_DATA_PROTECT &&
          Sense.AdditionalSenseCode == SFB_MSC_ASC_WRITE_PROTECTED);
  Lun.ReadOnly = FALSE;
}

static void
TestUnsupportedRequestSenseAndEject(void)
{
  UINT8 Cdb[16] = { 0 };
  SFB_MSC_SENSE Sense = { 0 };
  SFB_MSC_SCSI_RESPONSE Response;

  Cdb[0] = 0xFF;
  assert (Command (Cdb, 6, &Sense, &Response) == EFI_SUCCESS);
  assert (Response.ScsiStatus == SFB_MSC_SCSI_CHECK_CONDITION);
  assert (Sense.SenseKey == SFB_MSC_SENSE_ILLEGAL_REQUEST &&
          Sense.AdditionalSenseCode == SFB_MSC_ASC_INVALID_COMMAND);

  memset (Cdb, 0, sizeof (Cdb));
  Cdb[0] = 0x03;
  Cdb[4] = 18;
  assert (Command (Cdb, 6, &Sense, &Response) == EFI_SUCCESS);
  assert (Response.DataLength == 18 && Response.Data[2] ==
          SFB_MSC_SENSE_ILLEGAL_REQUEST && Response.Data[12] ==
          SFB_MSC_ASC_INVALID_COMMAND);
  assert (!Sense.Valid);
  assert (Command (Cdb, 6, &Sense, &Response) == EFI_SUCCESS);
  assert (Response.Data[2] == SFB_MSC_SENSE_NO_SENSE && Response.Data[12] == 0);

  memset (Cdb, 0, sizeof (Cdb));
  Cdb[0] = 0x1B;
  Cdb[4] = 2;
  assert (Command (Cdb, 6, &Sense, &Response) == EFI_SUCCESS);
  assert (Response.ScsiStatus == SFB_MSC_SCSI_GOOD && Response.Eject);
}

int
main(void)
{
  memset (&Lun, 0, sizeof (Lun));
  Lun.BlockIo = &BlockIo;
  memcpy (Lun.Vendor, "CANOE   ", sizeof (Lun.Vendor));
  memcpy (Lun.Product, "persist         ", sizeof (Lun.Product));
  TestCbwCswFraming ();
  TestInquiryAndCapacity ();
  TestReadWriteBoundsAndSense ();
  TestUnsupportedRequestSenseAndEject ();
  puts ("MSC tests passed");
  return 0;
}
