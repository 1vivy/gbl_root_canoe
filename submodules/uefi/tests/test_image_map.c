#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#undef NULL
#include "../edk2/Ext4Pkg/Ext4Dxe/Ext4Dxe.h"
#include <Library/Ext4ImageMap.h>
static EXT4_PARTITION P;
static EXT4_FILE F;
static EXT4_INODE Inode;
static EXT4_BLOCK_GROUP_DESC Group;
static EFI_BLOCK_IO_PROTOCOL Block;
static EFI_BLOCK_IO_MEDIA Media;
static unsigned char Bitmap[4096], Tree[4096];
VOID *EFIAPI AllocatePool(UINTN n) { return malloc(n); }
VOID *EFIAPI AllocateZeroPool(UINTN n) { return calloc(1,n); }
VOID EFIAPI FreePool(VOID *p) { free(p); }
VOID *EFIAPI CopyMem(VOID *a, CONST VOID *b, UINTN n) { return memcpy(a,b,n); }
VOID *EFIAPI ZeroMem(VOID *a, UINTN n) { return memset(a,0,n); }
INTN EFIAPI CompareMem(CONST VOID *a, CONST VOID *b, UINTN n) { return memcmp(a,b,n); }
EFI_STATUS EFIAPI Ext4ReadFile(EFI_FILE_PROTOCOL *f, UINTN *n, VOID *b) { (void)f;(void)n;(void)b;return EFI_UNSUPPORTED; }
EFI_STATUS Ext4ReadInode(EXT4_PARTITION *p, EXT4_INO_NR n, EXT4_INODE **out) { (void)p;(void)n;*out=malloc(sizeof(Inode));memcpy(*out,&Inode,sizeof(Inode));return EFI_SUCCESS; }
BOOLEAN Ext4FileIsReg(CONST EXT4_FILE *f) { return (f->Inode->i_mode & 0xf000)==0x8000; }
BOOLEAN Ext4CheckExtentChecksum(CONST EXT4_EXTENT_HEADER *h, CONST EXT4_FILE *f) { (void)h;(void)f;return TRUE; }
BOOLEAN Ext4VerifyBlockGroupDescChecksum(CONST EXT4_PARTITION *p, CONST EXT4_BLOCK_GROUP_DESC *d, UINT32 g) { (void)p;(void)d;(void)g;return TRUE; }
UINT32 CalculateCrc32c(CONST VOID *b, UINTN n, UINT32 seed) { (void)b;(void)n;return seed; }
EFI_STATUS Ext4ReadDiskIo(EXT4_PARTITION *p, VOID *out, UINTN n, UINT64 offset) {
 assert(offset==1024 && n==sizeof(p->SuperBlock));memcpy(out,&p->SuperBlock,n);return EFI_SUCCESS;
}
EFI_STATUS Ext4ReadBlocks(EXT4_PARTITION *p, VOID *out, UINTN n, EXT4_BLOCK_NR block) {
 (void)p; assert(n==1);
 if(block==3) memcpy(out,Bitmap,4096);
 else if(block==100) memcpy(out,Tree,4096);
 else return EFI_DEVICE_ERROR;
 return EFI_SUCCESS;
}
static EXT4_EXTENT_HEADER *header(void) { return (EXT4_EXTENT_HEADER *)Inode.i_data; }
static EXT4_EXTENT *extent(void) { return (EXT4_EXTENT *)(header()+1); }
static void reset(void) {
 memset(&P,0,sizeof(P));memset(&F,0,sizeof(F));memset(&Inode,0,sizeof(Inode));
 memset(&Group,0,sizeof(Group));memset(&Block,0,sizeof(Block));memset(&Media,0,sizeof(Media));
 memset(Bitmap,255,sizeof(Bitmap));memset(Tree,0,sizeof(Tree));
 P.BlockIo=&Block;Block.Media=&Media;Media.MediaPresent=TRUE;Media.MediaId=7;Media.BlockSize=4096;Media.LastBlock=16383;
 P.BlockSize=4096;P.NumberBlocks=16384;P.NumberBlockGroups=1;P.BlockGroups=&Group;P.DescSize=64;P.InodeSize=sizeof(Inode);
 P.SuperBlock.s_state=1;P.SuperBlock.s_blocks_per_group=16384;P.SuperBlock.s_inodes_per_group=128;
 P.FeaturesIncompat=EXT4_FEATURE_INCOMPAT_EXTENTS;
 Group.bg_block_bitmap_lo=3;Group.bg_inode_bitmap_lo=4;Group.bg_inode_table_lo=5;
 F.Protocol.Read=Ext4ReadFile;F.Partition=&P;F.Inode=&Inode;F.InodeNum=12;
 Inode.i_mode=0x8000;Inode.i_flags=EXT4_EXTENTS_FL;Inode.i_links=1;Inode.i_size_lo=EXT4_IMAGE_BYTES;
 header()->eh_magic=EXT4_EXTENT_HEADER_MAGIC;header()->eh_entries=1;header()->eh_max=4;
 extent()->ee_start_lo=512;extent()->ee_len=8192;
}
static void rejected(void) {
 EXT4_IMAGE_MAP *m=(void *)1;
 assert(EFI_ERROR(Ext4MapImage(&F.Protocol,&m)));assert(m==NULL);
}
int main(void) {
 EXT4_IMAGE_MAP *m;
 reset();assert(Ext4MapImage(&F.Protocol,&m)==EFI_SUCCESS);assert(m->Count==1);assert(m->Ranges[0].Physical==512*4096);assert(m->Ranges[0].Bytes==EXT4_IMAGE_BYTES);FreePool(m);
 reset();P.SuperBlock.s_state=0;rejected();
 reset();extent()->ee_len=0x8001;rejected();
 reset();extent()->ee_block=1;rejected();
 reset();extent()->ee_start_lo=4;rejected();
 reset();Bitmap[512/8]=0;rejected();
 reset();header()->eh_max=65535;rejected();
 reset();header()->eh_entries=2;extent()->ee_len=4096;extent()[1]=extent()[0];extent()[1].ee_block=4096;rejected();
 reset();extent()->ee_len=4096;rejected();
 reset();Inode.i_links=2;rejected();
 reset();Media.ReadOnly=TRUE;rejected();
 reset();header()->eh_depth=1;
 { EXT4_EXTENT_INDEX *x=(EXT4_EXTENT_INDEX *)(header()+1);EXT4_EXTENT_HEADER *h=(void *)Tree;EXT4_EXTENT *e=(void *)(h+1);
   memset(x,0,sizeof(*x));x->ei_leaf_lo=100;h->eh_magic=EXT4_EXTENT_HEADER_MAGIC;h->eh_entries=1;h->eh_max=340;e->ee_len=8192;e->ee_start_lo=512;
   assert(Ext4MapImage(&F.Protocol,&m)==EFI_SUCCESS);FreePool(m);
   e->ee_start_lo=100;rejected(); /* file data overlaps its extent-tree block */
   e->ee_start_lo=512;h->eh_depth=1;rejected();
 }
 puts("PASS image mapping: dense extent trees, holes, unwritten/overlapping/metadata/free blocks and dirty media");
 return 0;
}
