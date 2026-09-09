/* libc headers must precede every EDK2 header: ProcessorBind.h pushes hidden
 * symbol visibility and never pops it, so memset/memcpy/__assert_fail pulled in
 * afterwards become unlinkable hidden references. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* EDK2's Base.h defines NULL unconditionally; drop libc's spelling first. */
#undef NULL

#include <Uefi.h>

#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbFatClassify.h"

static void
SetLe16(UINT8 *Bytes, UINTN Offset, UINT16 Value)
{
  Bytes[Offset] = (UINT8)Value;
  Bytes[Offset + 1] = (UINT8)(Value >> 8);
}

static void
SetLe32(UINT8 *Bytes, UINTN Offset, UINT32 Value)
{
  Bytes[Offset] = (UINT8)Value;
  Bytes[Offset + 1] = (UINT8)(Value >> 8);
  Bytes[Offset + 2] = (UINT8)(Value >> 16);
  Bytes[Offset + 3] = (UINT8)(Value >> 24);
}

/*
 * The BIOS Parameter Block of /dev/block/by-name/logfs on a OnePlus 15
 * (SM8850), read byte for byte off the device. Every field the classifier
 * looks at lies below offset 40, so the leading bytes are the whole of the
 * evidence; the rest of the sector is boot code.
 *
 * 4096-byte sectors, one sector per cluster, 512 root entries, FAT12. Note
 * offset 36 - the 32-bit FAT size - holds 0xEA290180, boot code rather than a
 * field: a predicate that took a non-zero value there as proof of FAT32 would
 * misread this volume.
 */
static const UINT8 mDeviceFat12Logfs[] = {
  0xeb, 0x3c, 0x90, 0x4d, 0x53, 0x44, 0x4f, 0x53, 0x35, 0x2e, 0x30, 0x00,
  0x10, 0x01, 0x01, 0x00, 0x02, 0x00, 0x02, 0x00, 0x05, 0xf8, 0x01, 0x00,
  0x01, 0x00, 0x01, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x80, 0x01, 0x29, 0xea, 0x55, 0x73, 0xd2, 0x4e, 0x4f, 0x20, 0x4e, 0x41,
  0x4d, 0x45, 0x20, 0x20, 0x20, 0x20, 0x46, 0x41, 0x54, 0x31, 0x32, 0x20,
  0x20, 0x20, 0x33, 0xc9, 0x8e, 0xd1, 0xbc, 0xf0, 0x7b, 0x8e, 0xd9, 0xb8
};

/*
 * The same, for /dev/block/by-name/modem_a: FAT16, four sectors per cluster.
 * This one carries its sector count only in the 32-bit field at offset 32
 * (89600 sectors) with the 16-bit field at offset 19 left zero, which is the
 * documented way to describe a volume too large for the 16-bit count. A
 * FAT12/16 predicate that insisted on a non-zero 16-bit count would drop the
 * modem partition.
 */
static const UINT8 mDeviceFat16Modem[] = {
  0xeb, 0x3c, 0x90, 0x4d, 0x53, 0x57, 0x49, 0x4e, 0x34, 0x2e, 0x31, 0x00,
  0x10, 0x04, 0x01, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0xf8, 0x58, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5e, 0x01, 0x00,
  0x80, 0x00, 0x29, 0x06, 0xa2, 0xff, 0x5c, 0x4e, 0x4f, 0x20, 0x4e, 0x41,
  0x4d, 0x45, 0x20, 0x20, 0x20, 0x20, 0x46, 0x41, 0x54, 0x31, 0x36, 0x20,
  0x20, 0x20, 0x0e, 0x1f, 0xbe, 0x5b, 0x7c, 0xac, 0x22, 0xc0, 0x74, 0x0b
};

/* Load a real device BPB into a full sector and stamp the boot signature,
 * which lives at offset 510 - past the captured prefix. */
static void
LoadDeviceBpb(UINT8 *Sector, UINTN SectorSize, const UINT8 *Bpb, UINTN Bytes)
{
  memset (Sector, 0, SectorSize);
  memcpy (Sector, Bpb, Bytes);
  Sector[SFB_CLASSIFY_BPB_SIGNATURE] = 0x55;
  Sector[SFB_CLASSIFY_BPB_SIGNATURE + 1] = 0xaa;
}

/* Build a BPB that is valid apart from whatever the caller then breaks. */
static void
MakeFat32(UINT8 *Sector, UINTN SectorSize)
{
  memset (Sector, 0, SectorSize);
  Sector[SFB_CLASSIFY_BPB_SIGNATURE] = 0x55;
  Sector[SFB_CLASSIFY_BPB_SIGNATURE + 1] = 0xaa;
  SetLe16 (Sector, SFB_CLASSIFY_BPB_BYTES_PER_SEC, 512);
  Sector[SFB_CLASSIFY_BPB_SEC_PER_CLUS] = 8;
  SetLe16 (Sector, SFB_CLASSIFY_BPB_RSVD_SEC_CNT, 32);
  Sector[SFB_CLASSIFY_BPB_NUM_FATS] = 2;
  SetLe16 (Sector, SFB_CLASSIFY_BPB_ROOT_ENT_CNT, 0);
  SetLe16 (Sector, SFB_CLASSIFY_BPB_TOT_SEC_16, 0);
  SetLe16 (Sector, SFB_CLASSIFY_BPB_FAT_SZ_16, 0);
  SetLe32 (Sector, SFB_CLASSIFY_BPB_FAT_SZ_32, 1024);
}

static void
TestFat32WinsOverCoincidentalExt4Magic(void)
{
  UINT8 Sector[4096];

  MakeFat32 (Sector, sizeof (Sector));

  /* Make the false-positive ext4 fields plausible too. */
  SetLe32 (Sector, SFB_CLASSIFY_EXT4_INODES, 1);
  SetLe32 (Sector, SFB_CLASSIFY_EXT4_BLOCKS_LO, 1);
  SetLe32 (Sector, SFB_CLASSIFY_EXT4_LOG_BLOCK_SIZE, 0);
  SetLe16 (Sector, SFB_CLASSIFY_EXT4_MAGIC, 0xef53);
  SetLe16 (Sector, SFB_CLASSIFY_EXT4_INODE_SIZE, 128);

  assert(SfbClassifyVolumeBytes (Sector, sizeof (Sector)) == SfbVolumeKindFat);
}

static void
TestGenuineExt4RemainsExt4(void)
{
  UINT8 Sector[4096];

  memset (Sector, 0, sizeof (Sector));
  SetLe32 (Sector, SFB_CLASSIFY_EXT4_INODES, 1024);
  SetLe32 (Sector, SFB_CLASSIFY_EXT4_BLOCKS_LO, 4096);
  SetLe32 (Sector, SFB_CLASSIFY_EXT4_LOG_BLOCK_SIZE, 2);
  SetLe16 (Sector, SFB_CLASSIFY_EXT4_MAGIC, 0xef53);
  SetLe16 (Sector, SFB_CLASSIFY_EXT4_INODE_SIZE, 256);

  assert(SfbClassifyVolumeBytes (Sector, sizeof (Sector)) ==
         SfbVolumeKindExt4);
}

static void
TestInformationalFatTextIsNotEnough(void)
{
  UINT8 Sector[4096];

  memset (Sector, 0, sizeof (Sector));
  Sector[SFB_CLASSIFY_BPB_SIGNATURE] = 0x55;
  Sector[SFB_CLASSIFY_BPB_SIGNATURE + 1] = 0xaa;
  SetLe16 (Sector, SFB_CLASSIFY_BPB_BYTES_PER_SEC, 512);
  memcpy (Sector + 82, "FAT32   ", 8);

  assert(SfbClassifyVolumeBytes (Sector, sizeof (Sector)) ==
         SfbVolumeKindOther);
}

/*
 * The regression this file exists for: the device ships no FAT32 partition at
 * all, so a FAT32-only classifier retained none of its 11 FAT volumes and the
 * volume scan saw only persist.
 */
static void
TestDeviceFat12IsAccepted(void)
{
  UINT8 Sector[4096];

  LoadDeviceBpb (Sector, sizeof (Sector), mDeviceFat12Logfs,
                 sizeof (mDeviceFat12Logfs));

  assert(SfbClassifyVolumeBytes (Sector, sizeof (Sector)) == SfbVolumeKindFat);
}

static void
TestDeviceFat16IsAccepted(void)
{
  UINT8 Sector[4096];

  LoadDeviceBpb (Sector, sizeof (Sector), mDeviceFat16Modem,
                 sizeof (mDeviceFat16Modem));

  assert(SfbClassifyVolumeBytes (Sector, sizeof (Sector)) == SfbVolumeKindFat);
}

/*
 * A GPT protective MBR: the boot signature is present and every BPB field is
 * zero. Six of this device's block devices look exactly like this, and they
 * are what the geometry checks are for - the signature alone decides nothing.
 */
static void
TestProtectiveMbrIsNotFat(void)
{
  UINT8 Sector[4096];

  memset (Sector, 0, sizeof (Sector));
  Sector[SFB_CLASSIFY_BPB_SIGNATURE] = 0x55;
  Sector[SFB_CLASSIFY_BPB_SIGNATURE + 1] = 0xaa;

  assert(SfbClassifyVolumeBytes (Sector, sizeof (Sector)) ==
         SfbVolumeKindOther);
}

/* Neither width's field set is complete: no root directory to speak of and no
 * FAT size in either width. Accepting this would let a partition whose first
 * sector happens to end in 0x55AA mount as a boot volume. */
static void
TestNeitherWidthIsRejected(void)
{
  UINT8 Sector[4096];

  MakeFat32 (Sector, sizeof (Sector));
  SetLe32 (Sector, SFB_CLASSIFY_BPB_FAT_SZ_32, 0);

  assert(SfbClassifyVolumeBytes (Sector, sizeof (Sector)) ==
         SfbVolumeKindOther);
}

/* A FAT12/16 shape with no sector count in either width describes a volume of
 * no size. */
static void
TestFat16WithoutAnySectorCountIsRejected(void)
{
  UINT8 Sector[4096];

  LoadDeviceBpb (Sector, sizeof (Sector), mDeviceFat16Modem,
                 sizeof (mDeviceFat16Modem));
  SetLe16 (Sector, SFB_CLASSIFY_BPB_TOT_SEC_16, 0);
  SetLe32 (Sector, SFB_CLASSIFY_BPB_TOT_SEC_32, 0);

  assert(SfbClassifyVolumeBytes (Sector, sizeof (Sector)) ==
         SfbVolumeKindOther);
}

/* Sectors per cluster is a power of two by definition. A value like 3 is the
 * cheapest tell that a sector is not a BPB at all. */
static void
TestNonPowerOfTwoClusterSizeIsRejected(void)
{
  UINT8 Sector[4096];

  MakeFat32 (Sector, sizeof (Sector));
  Sector[SFB_CLASSIFY_BPB_SEC_PER_CLUS] = 3;

  assert(SfbClassifyVolumeBytes (Sector, sizeof (Sector)) ==
         SfbVolumeKindOther);
}

/* No FAT means no file system, whatever else the sector claims. */
static void
TestZeroFatCountIsRejected(void)
{
  UINT8 Sector[4096];

  LoadDeviceBpb (Sector, sizeof (Sector), mDeviceFat12Logfs,
                 sizeof (mDeviceFat12Logfs));
  Sector[SFB_CLASSIFY_BPB_NUM_FATS] = 0;

  assert(SfbClassifyVolumeBytes (Sector, sizeof (Sector)) ==
         SfbVolumeKindOther);
}

int
main(void)
{
  TestFat32WinsOverCoincidentalExt4Magic ();
  TestGenuineExt4RemainsExt4 ();
  TestInformationalFatTextIsNotEnough ();
  TestDeviceFat12IsAccepted ();
  TestDeviceFat16IsAccepted ();
  TestProtectiveMbrIsNotFat ();
  TestNeitherWidthIsRejected ();
  TestFat16WithoutAnySectorCountIsRejected ();
  TestNonPowerOfTwoClusterSizeIsRejected ();
  TestZeroFatCountIsRejected ();
  return 0;
}
