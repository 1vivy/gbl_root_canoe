#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL
#include "../edk2/QcomModulePkg/Library/FastbootLib/FastbootHash.h"
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
static UINTN Reads, Flushes, Allocations;
static BOOLEAN FailRead, FailFlush, FailAlloc;
VOID *EFIAPI CopyMem (VOID *D, CONST VOID *S, UINTN N) { return memcpy (D,S,N); }
VOID *EFIAPI SetMem (VOID *D, UINTN N, UINT8 V) { return memset (D,V,N); }
VOID *EFIAPI AllocateAlignedPages (UINTN N, UINTN A) {
  if (FailAlloc) return NULL;
  Allocations++;
  return aligned_alloc (A, N * EFI_PAGE_SIZE);
}
VOID EFIAPI FreeAlignedPages (VOID *P, UINTN N) { (void)N; Allocations--; free(P); }
static EFI_STATUS EFIAPI flush (EFI_BLOCK_IO_PROTOCOL *D) {
  (void)D; Flushes++; return FailFlush ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI read_blocks (EFI_BLOCK_IO_PROTOCOL *D, UINT32 Id, EFI_LBA Lba, UINTN N, VOID *B) {
  UINTN I; UINT64 Start = Lba * D->Media->BlockSize;
  assert (Id == D->Media->MediaId && N % D->Media->BlockSize == 0);
  assert ((UINTN)B % D->Media->IoAlign == 0);
  assert (Start + N <= (D->Media->LastBlock+1) * D->Media->BlockSize);
  Reads++;
  if (FailRead) return EFI_DEVICE_ERROR;
  for (I=0; I<N; I++) ((UINT8 *)B)[I] = (UINT8)((Start+I)%251);
  return EFI_SUCCESS;
}
int main (int argc, char **argv) {
  EFI_BLOCK_IO_MEDIA M = {0}; EFI_BLOCK_IO_PROTOCOL D = {0};
  CHAR8 Result[44]; CHAR16 Name[36]; UINT64 Offset, Length;
  M.MediaPresent=TRUE; M.MediaId=7; M.BlockSize=4096; M.LastBlock=262143; M.IoAlign=65536;
  D.Media=&M; D.ReadBlocks=read_blocks; D.FlushBlocks=flush;
  assert (SfbHashParse("persist:1:10",Name,&Offset,&Length)==EFI_SUCCESS && Offset==1 && Length==16);
  const char *bad[]={"", "persist", "persist:", "persist:0", "persist:0:", ":0:0", "../persist:0:0", "persist:0x0:1", "persist:0:1:2", "persist:10000000000000000:1"};
  for (size_t I=0; I<sizeof(bad)/sizeof(*bad); I++) assert(EFI_ERROR(SfbHashParse(bad[I],Name,&Offset,&Length)));
  assert(EFI_ERROR(SfbHashPartition(&D, MAX_UINT64, 1, Result)) && Reads==0);
  FailAlloc=TRUE; assert(SfbHashPartition(&D,0,1,Result)==EFI_OUT_OF_RESOURCES); FailAlloc=FALSE;
  FailFlush=TRUE; assert(SfbHashPartition(&D,0,1,Result)==EFI_DEVICE_ERROR && Reads==0); FailFlush=FALSE;
  FailRead=TRUE; assert(SfbHashPartition(&D,0,1,Result)==EFI_DEVICE_ERROR && !Result[0]); FailRead=FALSE;
  assert(Allocations==0);
  if(argc==3) {
    Offset=strtoull(argv[1],NULL,10); Length=strtoull(argv[2],NULL,10);
    assert(SfbHashPartition(&D,Offset,Length,Result)==EFI_SUCCESS);
    assert(strlen(Result)==43 && Allocations==0);
    puts(Result);
  }
  return 0;
}
