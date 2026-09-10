/* The production target helper against a byte-addressed misc BlockIo fixture. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#undef NULL
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/RebootTargetLib.h>
#include <Protocol/BlockIo.h>

EFI_GUID gEfiMiscPartitionGuid, gEfiBlockIoProtocolGuid;
static EFI_BOOT_SERVICES Bs;
EFI_BOOT_SERVICES *gBS = &Bs;
EFI_RUNTIME_SERVICES *gRT;
static EFI_BLOCK_IO_PROTOCOL Io;
static EFI_BLOCK_IO_MEDIA Media;
static unsigned char Misc[4096], Before[4096];
static UINTN Writes, Flushes;
static EFI_STATUS ReadError, WriteError, FlushError;
VOID *EFIAPI AllocateAlignedPages (UINTN Pages, UINTN Alignment) { (void)Alignment; return calloc(Pages, EFI_PAGE_SIZE); }
VOID EFIAPI FreeAlignedPages (VOID *Buffer, UINTN Pages) { (void)Pages; free(Buffer); }
VOID EFIAPI FreePool (VOID *Buffer) { free(Buffer); }
VOID *EFIAPI ZeroMem (VOID *Buffer, UINTN Size) { return memset(Buffer,0,Size); }
VOID *EFIAPI CopyMem (VOID *Out, CONST VOID *In, UINTN Size) { return memcpy(Out,In,Size); }
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN Size) { return memcmp(A,B,Size); }
UINTN EFIAPI __AsciiStrLen (CONST CHAR8 *Text) { return strlen(Text); }
RETURN_STATUS EFIAPI __StrCpyS (CHAR16 *Out, UINTN Capacity, CONST CHAR16 *In) {
  UINTN I=0; while(In[I]) { assert(I+1<Capacity); Out[I]=In[I]; I++; } Out[I]=0; return RETURN_SUCCESS;
}
static EFI_STATUS EFIAPI Locate (EFI_LOCATE_SEARCH_TYPE Search, EFI_GUID *Guid, VOID *Key, UINTN *Count, EFI_HANDLE **Handles) {
  (void)Search;(void)Guid;(void)Key; *Count=1; *Handles=malloc(sizeof(EFI_HANDLE)); **Handles=&Io; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Handle (EFI_HANDLE Device, EFI_GUID *Guid, VOID **Out) { (void)Device;(void)Guid; *Out=&Io; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI Read (EFI_BLOCK_IO_PROTOCOL *This, UINT32 Id, EFI_LBA Lba, UINTN Size, VOID *Out) {
  (void)This;(void)Id; assert(Lba==0 && Size==sizeof Misc);
  if (EFI_ERROR(ReadError)) return ReadError;
  memcpy(Out,Misc,Size); return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Write (EFI_BLOCK_IO_PROTOCOL *This, UINT32 Id, EFI_LBA Lba, UINTN Size, VOID *In) {
  (void)This;(void)Id; assert(Lba==0 && Size==sizeof Misc); Writes++;
  if (EFI_ERROR(WriteError)) return WriteError;
  memcpy(Misc,In,Size); return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Flush (EFI_BLOCK_IO_PROTOCOL *This) { (void)This; Flushes++; return FlushError; }
static void Reset (const char *Command) {
  for (UINTN I=0;I<sizeof Misc;I++) Misc[I]=(unsigned char)I;
  memset(Misc,0,32); strcpy((char *)Misc,Command); memcpy(Before,Misc,sizeof Misc);
  Writes=Flushes=0; ReadError=WriteError=FlushError=EFI_SUCCESS;
}
int main(void) {
  UINT8 Reason;
  Bs.LocateHandleBuffer=Locate; Bs.HandleProtocol=Handle;
  Media.BlockSize=sizeof Misc; Media.IoAlign=4096; Io.Media=&Media;
  Io.ReadBlocks=Read; Io.WriteBlocks=Write; Io.FlushBlocks=Flush;
  Reset(""); assert(RebootTargetPrepare(RebootTargetFastbootd,&Reason)==EFI_SUCCESS);
  assert(Reason==0 && strcmp((char *)Misc,"boot-fastboot")==0 && Writes==1 && Flushes==1);
  assert(memcmp(Misc+32,Before+32,sizeof Misc-32)==0);
  Reset("boot-fastboot"); assert(RebootTargetPrepare(RebootTargetSystem,&Reason)==EFI_SUCCESS);
  assert(Misc[0]==0 && Writes==1 && Flushes==1 && memcmp(Misc+32,Before+32,sizeof Misc-32)==0);
  Reset("vendor-preserve"); assert(RebootTargetPrepare(RebootTargetSystem,&Reason)==EFI_SUCCESS);
  assert(Writes==0 && memcmp(Misc,Before,sizeof Misc)==0);
  Reset(""); assert(RebootTargetPrepare(RebootTargetBootloader,&Reason)==EFI_SUCCESS && Reason==2 && Writes==0);
  Reset(""); FlushError=EFI_DEVICE_ERROR;
  assert(RebootTargetPrepare(RebootTargetRecovery,&Reason)==EFI_DEVICE_ERROR && Writes==1 && Flushes==1);
  Reset(""); ReadError=EFI_DEVICE_ERROR;
  assert(RebootTargetPrepare(RebootTargetRecovery,&Reason)==EFI_DEVICE_ERROR && Writes==0);
  Reset(""); WriteError=EFI_WRITE_PROTECTED;
  assert(RebootTargetPrepare(RebootTargetRecovery,&Reason)==EFI_WRITE_PROTECTED && Flushes==0);
  assert(RebootTargetPrepare(RebootTargetCount,&Reason)==EFI_INVALID_PARAMETER);
  puts("reboot targets: target selection, BCB preservation, stale-command clearing and read/write/flush failures passed");
  return 0;
}
