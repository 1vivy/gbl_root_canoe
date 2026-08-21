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

static void
TestFat32WinsOverCoincidentalExt4Magic(void)
{
  UINT8 Sector[4096];

  memset (Sector, 0, sizeof (Sector));
  Sector[SFB_CLASSIFY_BPB_SIGNATURE] = 0x55;
  Sector[SFB_CLASSIFY_BPB_SIGNATURE + 1] = 0xaa;
  SetLe16 (Sector, SFB_CLASSIFY_BPB_BYTES_PER_SEC, 512);
  SetLe16 (Sector, SFB_CLASSIFY_BPB_ROOT_ENT_CNT, 0);
  SetLe16 (Sector, SFB_CLASSIFY_BPB_TOT_SEC_16, 0);
  SetLe16 (Sector, SFB_CLASSIFY_BPB_FAT_SZ_16, 0);
  SetLe32 (Sector, SFB_CLASSIFY_BPB_FAT_SZ_32, 1);

  /* Make the false-positive ext4 fields plausible too. */
  SetLe32 (Sector, SFB_CLASSIFY_EXT4_INODES, 1);
  SetLe32 (Sector, SFB_CLASSIFY_EXT4_BLOCKS_LO, 1);
  SetLe32 (Sector, SFB_CLASSIFY_EXT4_LOG_BLOCK_SIZE, 0);
  SetLe16 (Sector, SFB_CLASSIFY_EXT4_MAGIC, 0xef53);
  SetLe16 (Sector, SFB_CLASSIFY_EXT4_INODE_SIZE, 128);

  assert(SfbClassifyVolumeBytes (Sector, sizeof (Sector)) ==
         SfbVolumeKindFat32);
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

int
main(void)
{
  TestFat32WinsOverCoincidentalExt4Magic ();
  TestGenuineExt4RemainsExt4 ();
  TestInformationalFatTextIsNotEnough ();
  return 0;
}
