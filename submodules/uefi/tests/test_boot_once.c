/* Production boot-once policy against a byte-addressed 4096-byte misc fixture. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/RebootTargetLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbBootOnce.h"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLaunchPolicy.h"

EFI_GUID gEfiMiscPartitionGuid, gEfiBlockIoProtocolGuid;
static EFI_BOOT_SERVICES Bs;
EFI_BOOT_SERVICES *gBS = &Bs;
EFI_RUNTIME_SERVICES *gRT;
static EFI_BLOCK_IO_PROTOCOL Io;
static EFI_BLOCK_IO_MEDIA Media;
static unsigned char Misc[4096], Before[4096];
static UINTN Writes, Flushes, Event, ClearEvent, LaunchEvent;
static EFI_STATUS ReadError, WriteError, FlushError;
static UINTN LaunchedIndex;

VOID *EFIAPI AllocateAlignedPages (UINTN Pages, UINTN Alignment) { (void)Alignment; return calloc(Pages, EFI_PAGE_SIZE); }
VOID EFIAPI FreeAlignedPages (VOID *Buffer, UINTN Pages) { (void)Pages; free(Buffer); }
VOID EFIAPI FreePool (VOID *Buffer) { free(Buffer); }
VOID *EFIAPI ZeroMem (VOID *Buffer, UINTN Size) { return memset(Buffer, 0, Size); }
VOID *EFIAPI CopyMem (VOID *Out, CONST VOID *In, UINTN Size) { return memcpy(Out, In, Size); }
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN Size) { return memcmp(A, B, Size); }
INTN EFIAPI AsciiStrCmp (CONST CHAR8 *A, CONST CHAR8 *B) { return strcmp(A, B); }
UINTN EFIAPI __AsciiStrLen (CONST CHAR8 *Text) { return strlen(Text); }
RETURN_STATUS EFIAPI __StrCpyS (CHAR16 *Out, UINTN Capacity, CONST CHAR16 *In) {
  UINTN I = 0; while (In[I]) { assert(I + 1 < Capacity); Out[I] = In[I]; I++; } Out[I] = 0; return RETURN_SUCCESS;
}
static EFI_STATUS EFIAPI Locate (EFI_LOCATE_SEARCH_TYPE Search, EFI_GUID *Guid, VOID *Key, UINTN *Count, EFI_HANDLE **Handles) {
  (void)Search; (void)Guid; (void)Key; *Count = 1; *Handles = malloc(sizeof(EFI_HANDLE)); **Handles = &Io; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Handle (EFI_HANDLE Device, EFI_GUID *Guid, VOID **Out) { (void)Device; (void)Guid; *Out = &Io; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI Read (EFI_BLOCK_IO_PROTOCOL *This, UINT32 Id, EFI_LBA Lba, UINTN Size, VOID *Out) {
  (void)This; (void)Id; assert(Lba == 0 && Size == sizeof Misc); if (EFI_ERROR(ReadError)) return ReadError; memcpy(Out, Misc, Size); return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Write (EFI_BLOCK_IO_PROTOCOL *This, UINT32 Id, EFI_LBA Lba, UINTN Size, VOID *In) {
  (void)This; (void)Id; assert(Lba == 0 && Size == sizeof Misc); Writes++; if (EFI_ERROR(WriteError)) return WriteError;
  memcpy(Misc, In, Size); if (Misc[0] == 0) ClearEvent = ++Event; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Flush (EFI_BLOCK_IO_PROTOCOL *This) { (void)This; Flushes++; return FlushError; }

static void ResetMisc (const char *Command) {
  for (UINTN I = 0; I < sizeof Misc; I++) Misc[I] = (unsigned char)I;
  memset(Misc, 0, 32); strcpy((char *)Misc, Command); memcpy(Before, Misc, sizeof Misc);
  Writes = Flushes = Event = ClearEvent = LaunchEvent = 0; LaunchedIndex = SFB_NO_INDEX;
  ReadError = WriteError = FlushError = EFI_SUCCESS;
}

VOID SfbBuildMenu (SFB_MENU_STATE *Menu, SFB_BOOT_MODE Mode, BOOLEAN FirstRun) {
  (void)Mode; (void)FirstRun; memset(Menu, 0, sizeof *Menu); Menu->Count = 4; Menu->ConfigValid = TRUE;
  Menu->Entry[0].Kind = SfbEntryEfiFile; strcpy(Menu->Entry[0].DefaultTarget, "android");
  Menu->Entry[1].Kind = SfbEntryBlsLinux; strcpy(Menu->Entry[1].DefaultTarget, "bls:pmos");
  Menu->Entry[2].Kind = SfbEntryFastboot; strcpy(Menu->Entry[2].DefaultTarget, "service");
  Menu->Entry[3].Kind = SfbEntryFastboot;
}
VOID SfbFreeMenu (SFB_MENU_STATE *Menu) { (void)Menu; }
VOID SfbSetLaunchLockPolicy (SFB_CONFIG_LOCK_POLICY Policy) { (void)Policy; }
EFI_STATUS SfbLaunchEntry (CONST SFB_BOOT_ENTRY *Entry, BOOLEAN Clear, SFB_BOOT_MODE Mode) {
  (void)Clear; (void)Mode; LaunchEvent = ++Event; assert(ClearEvent != 0 && ClearEvent < LaunchEvent);
  assert(Misc[0] == 0); assert(memcmp(Misc + 32, Before + 32, sizeof Misc - 32) == 0);
  if (strcmp(Entry->DefaultTarget, "android") == 0) LaunchedIndex = 0;
  else if (strcmp(Entry->DefaultTarget, "bls:pmos") == 0) LaunchedIndex = 1;
  else assert(!"unexpected launch target");
  return EFI_SUCCESS;
}

static void TestValidLaunchAndPreservation (void) {
  ResetMisc(""); assert(SfbBootOnceArm("android") == EFI_SUCCESS);
  assert(strcmp((char *)Misc, "canoe-once:android") == 0);
  assert(memcmp(Misc + 32, Before + 32, sizeof Misc - 32) == 0);
  memcpy(Before, Misc, sizeof Misc); Event = ClearEvent = LaunchEvent = 0; Writes = Flushes = 0;
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceMenu);
  assert(LaunchedIndex == 0 && Writes == 1 && Flushes == 1);
  assert(ClearEvent < LaunchEvent && memcmp(Misc + 32, Before + 32, sizeof Misc - 32) == 0);

  ResetMisc(""); assert(SfbBootOnceArm("bls:pmos") == EFI_SUCCESS); memcpy(Before, Misc, sizeof Misc);
  Event = ClearEvent = LaunchEvent = 0; assert(SfbBootOnceConsume(SfbBootModeHonestUnlocked) == SfbBootOnceMenu);
  assert(LaunchedIndex == 1 && ClearEvent < LaunchEvent);
}

static void TestFastbootAndUnresolved (void) {
  ResetMisc(""); assert(SfbBootOnceArm("service") == EFI_SUCCESS);
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceFastboot && LaunchedIndex == SFB_NO_INDEX);
  ResetMisc(""); assert(SfbBootOnceArm("fastboot") == EFI_SUCCESS);
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceFastboot && Misc[0] == 0);

  ResetMisc("canoe-once:missing");
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceMenu);
  assert(LaunchedIndex == SFB_NO_INDEX && Misc[0] == 0);
  assert(SfbBootOnceTakeRejectedNotice()); assert(!SfbBootOnceTakeRejectedNotice());
}

static void TestRefusalAndClearFailure (void) {
  char LongSelector[22] = "123456789012345678901";
  ResetMisc(""); assert(RebootTargetBootOnceArm(LongSelector) == EFI_INVALID_PARAMETER && Writes == 0);
  assert(RebootTargetBootOnceArm("bad/selector") == EFI_INVALID_PARAMETER && Writes == 0);
  assert(SfbBootOnceArm("missing") == EFI_NOT_FOUND && Writes == 0);

  ResetMisc(""); assert(SfbBootOnceArm("android") == EFI_SUCCESS); memcpy(Before, Misc, sizeof Misc);
  Event = ClearEvent = LaunchEvent = 0; LaunchedIndex = SFB_NO_INDEX; FlushError = EFI_DEVICE_ERROR;
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceNone);
  assert(LaunchedIndex == SFB_NO_INDEX && LaunchEvent == 0);

  ResetMisc("vendor-command"); assert(RebootTargetBootOnceClear() == EFI_SUCCESS);
  assert(Writes == 0 && memcmp(Misc, Before, sizeof Misc) == 0);
}

int main (void) {
  Bs.LocateHandleBuffer = Locate; Bs.HandleProtocol = Handle;
  Media.BlockSize = sizeof Misc; Media.IoAlign = 4096; Io.Media = &Media;
  Io.ReadBlocks = Read; Io.WriteBlocks = Write; Io.FlushBlocks = Flush;
  TestValidLaunchAndPreservation(); TestFastbootAndUnresolved(); TestRefusalAndClearFailure();
  puts("boot once: validated arm, clear-before-launch, exact resolution, preservation and failure refusal passed");
  return 0;
}
