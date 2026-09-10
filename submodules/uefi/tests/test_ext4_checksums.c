/* Exercise the production BaseLib CRC and Ext4 superblock/group admission.
 * Only disk I/O and the subsequent root-inode open are host substitutes. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL
#include "../edk2/Ext4Pkg/Ext4Dxe/Ext4Dxe.h"

static FILE *Image;
static unsigned RootOpens;

UINT64 EFIAPI DivU64x32(UINT64 value, UINT32 divisor) { return value / divisor; }
UINT64 EFIAPI DivU64x32Remainder(UINT64 value, UINT32 divisor, UINT32 *remainder) {
  if (remainder) *remainder = value % divisor;
  return value / divisor;
}
EFI_STATUS Ext4ReadDiskIo(EXT4_PARTITION *p, VOID *out, UINTN bytes, UINT64 offset) {
  (void)p;
  if (offset > 0x7fffffff || fseek(Image, (long)offset, SEEK_SET) != 0 ||
      fread(out, 1, bytes, Image) != bytes) return EFI_DEVICE_ERROR;
  return EFI_SUCCESS;
}
VOID *Ext4AllocAndReadBlocks(EXT4_PARTITION *p, UINTN count, EXT4_BLOCK_NR block) {
  UINTN bytes = count * p->BlockSize;
  VOID *out = malloc(bytes);
  if (out && EFI_ERROR(Ext4ReadDiskIo(p, out, bytes, block * p->BlockSize))) {
    free(out); out = NULL;
  }
  return out;
}
EFI_STATUS EFIAPI Ext4OpenVolume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs, EFI_FILE_PROTOCOL **root) {
  (void)fs; *root = NULL; RootOpens++; return EFI_SUCCESS;
}

int main(int argc, char **argv) {
  EXT4_PARTITION p = {0};
  EFI_STATUS status;
  UINT16 split;
  assert(CalculateCrc16Ansi("", 0, CRC16ANSI_INIT) == 0xffff);
  assert(CalculateCrc16Ansi("123456789", 9, CRC16ANSI_INIT) == 0x4b37);
  split = CalculateCrc16Ansi("1234", 4, CRC16ANSI_INIT);
  assert(CalculateCrc16Ansi("56789", 5, split) == 0x4b37);
  assert(argc == 3);
  Image = fopen(argv[1], "rb"); assert(Image != NULL);
  status = Ext4OpenSuperblock(&p);
  if (strcmp(argv[2], "reject") == 0) {
    assert(status == EFI_VOLUME_CORRUPTED); assert(RootOpens == 0);
    puts("PASS corrupted group rejected before root open");
  } else {
    assert(strcmp(argv[2], "accept") == 0);
    assert(status == EFI_SUCCESS); assert(RootOpens == 1);
    assert(p.NumberBlockGroups == (p.NumberBlocks - p.SuperBlock.s_first_data_block +
           p.SuperBlock.s_blocks_per_group - 1) / p.SuperBlock.s_blocks_per_group);
    assert(Ext4HasGdtCsum(&p) || Ext4HasMetadataCsum(&p));
    for (UINT32 group = 0; group < p.NumberBlockGroups; group++) {
      EXT4_BLOCK_GROUP_DESC *d = Ext4GetBlockGroupDesc(&p, group);
      UINT8 *bytes = (UINT8 *)d;
      assert(Ext4VerifyBlockGroupDescChecksum(&p, d, group));
      assert(!Ext4VerifyBlockGroupDescChecksum(&p, d, group + 1));
      /* Each individual descriptor byte, including high fields, is covered. */
      for (UINTN i = 0; i < p.DescSize; i++) {
        bytes[i] ^= 1;
        assert(!Ext4VerifyBlockGroupDescChecksum(&p, d, group));
        bytes[i] ^= 1;
      }
    }
    printf("PASS production ext4 admission: groups=%llu descriptor=%u recover=%u\n",
           (unsigned long long)p.NumberBlockGroups, (unsigned)p.DescSize,
           !!(p.FeaturesIncompat & EXT4_FEATURE_INCOMPAT_RECOVER));
  }
  free(p.BlockGroups); fclose(Image); return 0;
}
