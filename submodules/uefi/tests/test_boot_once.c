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
static UINTN Writes, Flushes, Event, ClearEvent, TargetWriteEvent, ReadyEvent,
             LaunchEvent;
static EFI_STATUS ReadError, WriteError, FlushError;
static UINTN LaunchedIndex;
static BOOLEAN ManagedAndroid, FailTargetWrite, FailLaunch, FailReady,
               ConfiguredDefault, CursorDefault, RequireClearBeforeLaunch;
static SFB_BOOT_MODE LaunchedMode;
static BOOLEAN TargetPendingSeen;

VOID *EFIAPI AllocateAlignedPages (UINTN Pages, UINTN Alignment) { (void)Alignment; return calloc(Pages, EFI_PAGE_SIZE); }
VOID EFIAPI FreeAlignedPages (VOID *Buffer, UINTN Pages) { (void)Pages; free(Buffer); }
VOID EFIAPI FreePool (VOID *Buffer) { free(Buffer); }
VOID *EFIAPI ZeroMem (VOID *Buffer, UINTN Size) { return memset(Buffer, 0, Size); }
VOID *EFIAPI CopyMem (VOID *Out, CONST VOID *In, UINTN Size) { return memcpy(Out, In, Size); }
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN Size) { return memcmp(A, B, Size); }
INTN EFIAPI AsciiStrCmp (CONST CHAR8 *A, CONST CHAR8 *B) { return strcmp(A, B); }
INTN EFIAPI AsciiStrnCmp (CONST CHAR8 *A, CONST CHAR8 *B, UINTN Length) { return strncmp(A, B, Length); }
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
  (void)This; (void)Id; assert(Lba == 0 && Size == sizeof Misc); Writes++;
  if (EFI_ERROR(WriteError)) return WriteError;
  /*
   * Positive recognition of every transient target command, which arrives here
   * as the whole zeroed 32-byte field. The event counter is what lets the tests
   * assert that the Canoe record is cleared before the command is written and
   * that the command is written before anything launches.
   */
  if (memcmp(In, "boot-recovery", sizeof("boot-recovery")) == 0 ||
      memcmp(In, "boot-fastboot", sizeof("boot-fastboot")) == 0 ||
      memcmp(In, "surfacer-menu", sizeof("surfacer-menu")) == 0) {
    TargetWriteEvent = ++Event;
    if (FailTargetWrite) return EFI_DEVICE_ERROR;
  }
  memcpy(Misc, In, Size); if (Misc[0] == 0) ClearEvent = ++Event; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Flush (EFI_BLOCK_IO_PROTOCOL *This) { (void)This; Flushes++; return FlushError; }

static void ResetMisc (const char *Command) {
  for (UINTN I = 0; I < sizeof Misc; I++) Misc[I] = (unsigned char)I;
  memset(Misc, 0, 32); strcpy((char *)Misc, Command); memcpy(Before, Misc, sizeof Misc);
  Writes = Flushes = Event = ClearEvent = TargetWriteEvent = ReadyEvent =
    LaunchEvent = 0;
  LaunchedIndex = SFB_NO_INDEX; ManagedAndroid = TRUE; FailTargetWrite = FALSE;
  FailLaunch = FALSE; FailReady = FALSE;
  ConfiguredDefault = TRUE; CursorDefault = FALSE;
  RequireClearBeforeLaunch = TRUE;
  LaunchedMode = SfbBootModeHonestUnlocked; TargetPendingSeen = FALSE;
  ReadError = WriteError = FlushError = EFI_SUCCESS;
}

/* SuperFbEntries.c is not linked here, so the managed-row predicate it owns is
 * faked from the fixture: the real one is TRUE only for a non-USB EFI file on a
 * canonical managed path, which the "android" row stands in for. */
VOID SfbBuildMenu (SFB_MENU_STATE *Menu, SFB_BOOT_MODE Mode, BOOLEAN FirstRun) {
  (void)FirstRun; memset(Menu, 0, sizeof *Menu); Menu->Count = 4;
  Menu->ConfigValid = TRUE; Menu->Mode = Mode;
  Menu->Entry[0].Kind = SfbEntryEfiFile; strcpy(Menu->Entry[0].DefaultTarget, "android");
  Menu->Entry[1].Kind = SfbEntryBlsLinux; strcpy(Menu->Entry[1].DefaultTarget, "bls:pmos");
  Menu->Entry[2].Kind = SfbEntryFastboot; strcpy(Menu->Entry[2].DefaultTarget, "service");
  Menu->Entry[3].Kind = SfbEntryFastboot;
  Menu->DefaultFromConfig = ConfiguredDefault;
  Menu->DefaultIndex = ConfiguredDefault ? 1 : (CursorDefault ? 0 : SFB_NO_INDEX);
}
BOOLEAN SfbIsManagedAblEntry (CONST SFB_BOOT_ENTRY *Entry) {
  return (BOOLEAN)(ManagedAndroid && Entry != NULL &&
                   strcmp(Entry->DefaultTarget, "android") == 0);
}
VOID SfbFreeMenu (SFB_MENU_STATE *Menu) { (void)Menu; }
VOID SfbSetLaunchLockPolicy (SFB_CONFIG_LOCK_POLICY Policy) { (void)Policy; }
EFI_STATUS SfbLaunchEntry (CONST SFB_BOOT_ENTRY *Entry, BOOLEAN Clear, SFB_BOOT_MODE Mode) {
  (void)Clear; LaunchEvent = ++Event;
  if (RequireClearBeforeLaunch) {
    assert(ClearEvent != 0 && ClearEvent < LaunchEvent);
  }
  if (ReadyEvent != 0) assert(ReadyEvent < LaunchEvent);
  if (TargetWriteEvent != 0) {
    assert(TargetWriteEvent < LaunchEvent);
    assert(strncmp((char *)Misc, "boot-", 5) == 0 ||
           strcmp((char *)Misc, "surfacer-menu") == 0);
  } else if (RequireClearBeforeLaunch) {
    assert(Misc[0] == 0);
  }
  assert(memcmp(Misc + 32, Before + 32, sizeof Misc - 32) == 0);
  if (strcmp(Entry->DefaultTarget, "android") == 0) LaunchedIndex = 0;
  else if (strcmp(Entry->DefaultTarget, "bls:pmos") == 0) LaunchedIndex = 1;
  else assert(!"unexpected launch target");
  LaunchedMode = Mode;
  return FailLaunch ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

/* The handoff callback the caller would pass. It records that the host has been
 * answered and the link handed over, and can report a failed release the way
 * CmdOemBootLaunchReady does. */
static EFI_STATUS ReadyToLaunch (VOID *Context) {
  assert(Context == &ReadyEvent);
  ReadyEvent = ++Event;
  return FailReady ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

static void TestOemParser (void) {
  SFB_BOOT_ONCE_VERB Verb;
  REBOOT_BOOT_ONCE_TARGET Target;
  CHAR8 Selector[REBOOT_BOOT_ONCE_SELECTOR_BYTES];

  /* Both spellings of the arm verb, the direct verb, clear, and the one
   * selector that is not a record name. */
  assert(SfbBootOnceParseOemArg("boot-once:android", &Verb, Selector, &Target) == EFI_SUCCESS);
  assert(Verb == SfbBootOnceVerbOnce && Target == RebootBootOnceTargetNone &&
         strcmp(Selector, "android") == 0);
  assert(SfbBootOnceParseOemArg("boot-once android", &Verb, Selector, &Target) == EFI_SUCCESS);
  assert(Verb == SfbBootOnceVerbOnce && Target == RebootBootOnceTargetNone);
  assert(SfbBootOnceParseOemArg("boot-once android recovery", &Verb, Selector, &Target) == EFI_SUCCESS);
  assert(Verb == SfbBootOnceVerbOnce && Target == RebootBootOnceTargetRecovery);
  assert(SfbBootOnceParseOemArg("boot-direct bls:pmos fastbootd", &Verb, Selector, &Target) == EFI_SUCCESS);
  assert(Verb == SfbBootOnceVerbDirect && Target == RebootBootOnceTargetFastbootd &&
         strcmp(Selector, "bls:pmos") == 0);
  assert(SfbBootOnceParseOemArg("boot-direct android menu", &Verb, Selector, &Target) == EFI_SUCCESS);
  assert(Verb == SfbBootOnceVerbDirect && Target == RebootBootOnceTargetMenu &&
         strcmp(Selector, "android") == 0);
  assert(SfbBootOnceParseOemArg("boot-direct default", &Verb, Selector, &Target) == EFI_SUCCESS);
  assert(Verb == SfbBootOnceVerbDirect && Target == RebootBootOnceTargetNone &&
         strcmp(Selector, "default") == 0);
  assert(SfbBootOnceParseOemArg("boot-once-clear", &Verb, Selector, &Target) == EFI_SUCCESS);
  assert(Verb == SfbBootOnceVerbClear && Selector[0] == '\0');

  /* The arm budget is the untagged field: 20 bytes, and exactly at the limit is
   * still a record. */
  assert(SfbBootOnceParseOemArg("boot-once 12345678901234567890", &Verb, Selector, &Target) == EFI_SUCCESS);
  assert(SfbBootOnceParseOemArg("boot-once 123456789012345678901", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(SfbBootOnceParseOemArg("boot-once:12345678901234567890", &Verb, Selector, &Target) == EFI_SUCCESS);
  assert(SfbBootOnceParseOemArg("boot-once:123456789012345678901", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);

  /* A tag spends its delimiter and its literal out of that same field, so the
   * selector budget shrinks by one byte for the delimiter and one per tag byte. */
  assert(SfbBootOnceParseOemArg("boot-once abcdefghijk recovery", &Verb, Selector, &Target) == EFI_SUCCESS);
  assert(SfbBootOnceParseOemArg("boot-once abcdefghijkl recovery", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(SfbBootOnceParseOemArg("boot-once abcdefghij fastbootd", &Verb, Selector, &Target) == EFI_SUCCESS);
  assert(SfbBootOnceParseOemArg("boot-once abcdefghijk fastbootd", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(SfbBootOnceParseOemArg("boot-direct abcdefghij fastbootd", &Verb, Selector, &Target) == EFI_SUCCESS);
  assert(SfbBootOnceParseOemArg("boot-direct abcdefghijk fastbootd", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(SfbBootOnceParseOemArg("boot-direct 12345678901234567890 recovery", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(SfbBootOnceParseOemArg("boot-direct 123456789012345 menu", &Verb, Selector, &Target) == EFI_SUCCESS);
  assert(SfbBootOnceParseOemArg("boot-direct 1234567890123456 menu", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);

  /* An argument that names no boot-once form at all stays available to the
   * caller's other OEM commands; one that names a verb and then breaks is
   * reported as that verb's malformed form. */
  assert(SfbBootOnceParseOemArg("boot-later android", &Verb, Selector, &Target) == EFI_SUCCESS);
  assert(Verb == SfbBootOnceVerbNone && Selector[0] == '\0' && Target == RebootBootOnceTargetNone);
  assert(SfbBootOnceParseOemArg("mass-storage:persist", &Verb, Selector, &Target) == EFI_SUCCESS);
  assert(Verb == SfbBootOnceVerbNone);
  assert(SfbBootOnceParseOemArg("log-flush", &Verb, Selector, &Target) == EFI_SUCCESS && Verb == SfbBootOnceVerbNone);
  assert(SfbBootOnceParseOemArg("boot-onceful android", &Verb, Selector, &Target) == EFI_SUCCESS && Verb == SfbBootOnceVerbNone);

  assert(SfbBootOnceParseOemArg("boot-once", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(Verb == SfbBootOnceVerbOnce);
  assert(SfbBootOnceParseOemArg("boot-direct", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(Verb == SfbBootOnceVerbDirect);
  assert(SfbBootOnceParseOemArg("boot-once ", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(SfbBootOnceParseOemArg("boot-once  android", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(SfbBootOnceParseOemArg("boot-direct  android", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(SfbBootOnceParseOemArg("boot-once android ", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(SfbBootOnceParseOemArg("boot-direct ", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(SfbBootOnceParseOemArg("boot-direct android unknown", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(Verb == SfbBootOnceVerbDirect && Target == RebootBootOnceTargetNone);
  assert(SfbBootOnceParseOemArg("boot-direct android recovery extra", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(SfbBootOnceParseOemArg("boot-direct android RECOVERY", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(SfbBootOnceParseOemArg("boot-direct bad/selector", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);

  /* `default` is a direct-form convenience only: a record has to name a target
   * that still exists on the next boot, in either spelling. */
  assert(SfbBootOnceParseOemArg("boot-once default", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(SfbBootOnceParseOemArg("boot-once:default", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(SfbBootOnceParseOemArg("boot-once default recovery", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);

  /* The legacy colon form is the plain record: no target slot, so a space after
   * the selector is a malformed record and never a tag. */
  assert(SfbBootOnceParseOemArg("boot-once:android recovery", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(SfbBootOnceParseOemArg("boot-once:android+recovery", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(SfbBootOnceParseOemArg("boot-once:", &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(SfbBootOnceParseOemArg(NULL, &Verb, Selector, &Target) == EFI_INVALID_PARAMETER);
  assert(Verb == SfbBootOnceVerbNone);
}

/* One request shape for every direct case, so a test cannot pass by hand-building
 * a form the parser would refuse. Ready is the handoff callback the caller would
 * pass: ReadyToLaunch for a launch, NULL for the stored-record path. */
static SFB_BOOT_ONCE_EXEC_RESULT RunDirect (
  const char              *Selector,
  REBOOT_BOOT_ONCE_TARGET  Target,
  SFB_BOOT_MODE            Mode,
  BOOLEAN                  AllowDefault,
  SFB_BOOT_ONCE_READY      Ready
  )
{
  SFB_BOOT_ONCE_REQUEST Request;

  Request.Selector = Selector;
  Request.Target = Target;
  Request.Mode = Mode;
  Request.AllowDefault = AllowDefault;
  return SfbBootOnceExecute(&Request, Ready, &ReadyEvent, &TargetPendingSeen);
}

static void TestDirectLaunch (void) {
  /* `default` resolves through canoe.cfg's default only, and launches it with the
   * session mode the caller supplied - never with a mode this path invents. */
  ResetMisc(""); RequireClearBeforeLaunch = FALSE;
  assert(RunDirect("default", RebootBootOnceTargetNone, SfbBootModeKmProfile, TRUE, ReadyToLaunch) == SfbBootOnceExecReturned);
  assert(LaunchedIndex == 1 && LaunchedMode == SfbBootModeKmProfile);
  assert(Writes == 0 && TargetWriteEvent == 0 && ReadyEvent < LaunchEvent);

  /* The cursor fallback is not a configured default: DefaultIndex alone, with
   * DefaultFromConfig unset, must not become an unattended launch here either. */
  ResetMisc(""); RequireClearBeforeLaunch = FALSE;
  ConfiguredDefault = FALSE; CursorDefault = TRUE;
  assert(RunDirect("default", RebootBootOnceTargetNone, SfbBootModeAblFakeLocked, TRUE, ReadyToLaunch) == SfbBootOnceExecFailed);
  assert(LaunchEvent == 0 && Writes == 0 && ReadyEvent == 0);

  ResetMisc(""); RequireClearBeforeLaunch = FALSE; ConfiguredDefault = FALSE;
  assert(RunDirect("default", RebootBootOnceTargetNone, SfbBootModeAblFakeLocked, TRUE, ReadyToLaunch) == SfbBootOnceExecFailed);
  assert(LaunchEvent == 0 && Writes == 0);

  /* A tag on a row that may not spend it is refused with misc untouched, and so
   * is a tag on a `default` that resolves to such a row. No refusal reaches the
   * handoff, which is what keeps the caller owning the link. */
  ResetMisc(""); RequireClearBeforeLaunch = FALSE;
  assert(RunDirect("bls:pmos", RebootBootOnceTargetRecovery, SfbBootModeHonestUnlocked, TRUE, ReadyToLaunch) == SfbBootOnceExecFailed);
  assert(LaunchEvent == 0 && ReadyEvent == 0 && Writes == 0 && TargetWriteEvent == 0);
  ResetMisc(""); RequireClearBeforeLaunch = FALSE;
  assert(RunDirect("default", RebootBootOnceTargetRecovery, SfbBootModeHonestUnlocked, TRUE, ReadyToLaunch) == SfbBootOnceExecFailed);
  assert(LaunchEvent == 0 && Writes == 0 && TargetWriteEvent == 0);

  /* A selector that resolves to nothing is a failure, not a menu fallback. */
  ResetMisc(""); RequireClearBeforeLaunch = FALSE;
  assert(RunDirect("missing", RebootBootOnceTargetNone, SfbBootModeHonestUnlocked, TRUE, ReadyToLaunch) == SfbBootOnceExecFailed);
  assert(LaunchEvent == 0 && Writes == 0);

  /* Untagged direct: nothing is written to misc and the launch is still reached. */
  ResetMisc(""); RequireClearBeforeLaunch = FALSE;
  assert(RunDirect("android", RebootBootOnceTargetNone, SfbBootModeHonestUnlocked, TRUE, ReadyToLaunch) == SfbBootOnceExecReturned);
  assert(LaunchedIndex == 0 && Writes == 0 && Flushes == 0 && TargetWriteEvent == 0);
  assert(ReadyEvent < LaunchEvent && Misc[0] == 0 && !TargetPendingSeen);

  /* Tagged direct on the managed row: the standard command is written, and it is
   * written before the host is answered and before anything launches. */
  ResetMisc(""); RequireClearBeforeLaunch = FALSE;
  assert(RunDirect("android", RebootBootOnceTargetRecovery, SfbBootModeAblFakeLocked, TRUE, ReadyToLaunch) == SfbBootOnceExecReturned);
  assert(LaunchedIndex == 0 && Writes == 1 && Flushes == 1);
  assert(TargetWriteEvent < ReadyEvent && ReadyEvent < LaunchEvent);
  assert(strcmp((char *)Misc, "boot-recovery") == 0);
  assert(TargetPendingSeen);
  assert(memcmp(Misc + 32, Before + 32, sizeof Misc - 32) == 0);

  ResetMisc(""); RequireClearBeforeLaunch = FALSE;
  assert(RunDirect("android", RebootBootOnceTargetFastbootd, SfbBootModeAblFakeLocked, TRUE, ReadyToLaunch) == SfbBootOnceExecReturned);
  assert(strcmp((char *)Misc, "boot-fastboot") == 0 && TargetWriteEvent < LaunchEvent);

  ResetMisc(""); RequireClearBeforeLaunch = FALSE;
  assert(RunDirect("android", RebootBootOnceTargetMenu, SfbBootModeAblFakeLocked, TRUE, ReadyToLaunch) == SfbBootOnceExecReturned);
  assert(strcmp((char *)Misc, "surfacer-menu") == 0);
  assert(TargetWriteEvent < ReadyEvent && ReadyEvent < LaunchEvent);
  assert(TargetPendingSeen);

  /* A standard command that cannot be written stops the launch: no answer has
   * been sent yet, so the host still learns about it over the intact link. */
  ResetMisc(""); RequireClearBeforeLaunch = FALSE; FailTargetWrite = TRUE;
  assert(RunDirect("android", RebootBootOnceTargetRecovery, SfbBootModeAblFakeLocked, TRUE, ReadyToLaunch) == SfbBootOnceExecFailed);
  assert(TargetWriteEvent != 0 && LaunchEvent == 0 && ReadyEvent == 0);
  assert(Writes == 1 && Misc[0] == 0 && !TargetPendingSeen);

  /*
   * Once the handoff has run the link belongs to the launch, so a launch that
   * then fails must still come back as a handoff: reporting a failure here would
   * have the caller send FAIL over a gadget that is already stopped and skip the
   * reconnect that brings the session back.
   */
  ResetMisc(""); RequireClearBeforeLaunch = FALSE; FailLaunch = TRUE;
  assert(RunDirect("android", RebootBootOnceTargetNone, SfbBootModeHonestUnlocked, TRUE, ReadyToLaunch) == SfbBootOnceExecReturned);
  assert(LaunchedIndex == 0 && ReadyEvent != 0 && ReadyEvent < LaunchEvent);
  assert(Writes == 0 && SfbBootOnceTakeNotice() == SfbBootOnceNoticeNone);

  ResetMisc(""); RequireClearBeforeLaunch = FALSE; FailLaunch = TRUE;
  assert(RunDirect("android", RebootBootOnceTargetRecovery, SfbBootModeAblFakeLocked, TRUE, ReadyToLaunch) == SfbBootOnceExecReturned);
  assert(TargetWriteEvent < ReadyEvent && ReadyEvent < LaunchEvent);
  assert(strcmp((char *)Misc, "boot-recovery") == 0);

  /* Without a handoff the link is still ours, so a failed launch is a failure. */
  ResetMisc(""); RequireClearBeforeLaunch = FALSE; FailLaunch = TRUE;
  assert(RunDirect("android", RebootBootOnceTargetNone, SfbBootModeHonestUnlocked, TRUE, NULL) == SfbBootOnceExecFailed);
  assert(LaunchEvent != 0 && ReadyEvent == 0);

  ResetMisc(""); RequireClearBeforeLaunch = FALSE; FailLaunch = TRUE;
  assert(RunDirect("android", RebootBootOnceTargetRecovery, SfbBootModeAblFakeLocked, TRUE, NULL) == SfbBootOnceExecFailed);
  assert(LaunchEvent != 0 && ReadyEvent == 0);
  assert(strcmp((char *)Misc, "boot-recovery") == 0);

  /*
   * A release that failed retains the controller, so nothing launches into it:
   * the child would inherit a gadget this image could not give up. The host has
   * already been answered, so the result stays the handoff one - that is what
   * makes the caller reconnect and try to restore the link - and the launch is
   * never attempted.
   */
  ResetMisc(""); RequireClearBeforeLaunch = FALSE; FailReady = TRUE;
  assert(RunDirect("android", RebootBootOnceTargetNone, SfbBootModeHonestUnlocked, TRUE, ReadyToLaunch) == SfbBootOnceExecReturned);
  assert(ReadyEvent != 0 && LaunchEvent == 0 && LaunchedIndex == SFB_NO_INDEX);
  assert(Writes == 0 && TargetWriteEvent == 0);

  ResetMisc(""); RequireClearBeforeLaunch = FALSE; FailReady = TRUE;
  assert(RunDirect("android", RebootBootOnceTargetRecovery, SfbBootModeAblFakeLocked, TRUE, ReadyToLaunch) == SfbBootOnceExecReturned);
  assert(TargetWriteEvent < ReadyEvent && LaunchEvent == 0 && LaunchedIndex == SFB_NO_INDEX);
  assert(strcmp((char *)Misc, "boot-recovery") == 0 && TargetPendingSeen);

  /* The stored-record path passes no callback, so it cannot see either failure
   * and keeps reporting the pre-handoff failure to its notice. */
  ResetMisc("canoe-once:android"); FailReady = TRUE; FailLaunch = TRUE;
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceMenu);
  assert(LaunchEvent != 0 && ReadyEvent == 0);
  assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeRecordDropped);

  /* The Super Fastboot row is answered, not re-entered: nothing is launched and
   * no standard command is written for it. */
  ResetMisc(""); RequireClearBeforeLaunch = FALSE;
  assert(RunDirect("fastboot", RebootBootOnceTargetNone, SfbBootModeHonestUnlocked, TRUE, ReadyToLaunch) == SfbBootOnceExecFastboot);
  assert(LaunchEvent == 0 && ReadyEvent == 0 && Writes == 0);
  assert(RunDirect("service", RebootBootOnceTargetNone, SfbBootModeHonestUnlocked, TRUE, ReadyToLaunch) == SfbBootOnceExecFastboot);
  assert(LaunchEvent == 0 && Writes == 0);
}

static void TestPlainRecordCompat (void) {
  char Twenty[] = "12345678901234567890";

  /* The record an older firmware wrote is still consumed, unchanged. */
  ResetMisc("canoe-once:android");
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceMenu);
  assert(LaunchedIndex == 0 && Writes == 1 && Flushes == 1);
  assert(ClearEvent < LaunchEvent && TargetWriteEvent == 0);
  assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeNone);
  assert(memcmp(Misc + 32, Before + 32, sizeof Misc - 32) == 0);

  ResetMisc(""); assert(SfbBootOnceArm("android", RebootBootOnceTargetNone) == EFI_SUCCESS);
  assert(strcmp((char *)Misc, "canoe-once:android") == 0);
  assert(memcmp(Misc + 32, Before + 32, sizeof Misc - 32) == 0);
  ResetMisc(""); assert(RebootTargetBootOnceArm(Twenty, RebootBootOnceTargetNone) == EFI_SUCCESS);
  assert(strcmp((char *)Misc, "canoe-once:12345678901234567890") == 0);

  ResetMisc(""); assert(SfbBootOnceArm("bls:pmos", RebootBootOnceTargetNone) == EFI_SUCCESS);
  memcpy(Before, Misc, sizeof Misc); Event = ClearEvent = LaunchEvent = 0; Writes = Flushes = 0;
  assert(SfbBootOnceConsume(SfbBootModeHonestUnlocked) == SfbBootOnceMenu);
  assert(LaunchedIndex == 1 && ClearEvent < LaunchEvent);
}

static void TestArmBudgets (void) {
  char Eleven[] = "abcdefghijk";
  char Twelve[] = "abcdefghijkl";
  char Ten[] = "abcdefghij";
  char TwentyOne[] = "123456789012345678901";
  char Fifteen[] = "123456789012345";
  char Sixteen[] = "1234567890123456";

  ResetMisc("");
  assert(SfbBootOnceArm("android", RebootBootOnceTargetRecovery) == EFI_SUCCESS);
  assert(strcmp((char *)Misc, "canoe-once:android+recovery") == 0);
  assert(memcmp(Misc + 32, Before + 32, sizeof Misc - 32) == 0);

  /* 11 bytes is all "recovery" leaves: 11 + 11 + 1 + 8 + NUL is the whole field. */
  ResetMisc("");
  assert(RebootTargetBootOnceArm(Eleven, RebootBootOnceTargetRecovery) == EFI_SUCCESS);
  assert(strcmp((char *)Misc, "canoe-once:abcdefghijk+recovery") == 0);
  assert(Misc[31] == 0 && memcmp(Misc + 32, Before + 32, sizeof Misc - 32) == 0);
  ResetMisc("");
  assert(RebootTargetBootOnceArm(Twelve, RebootBootOnceTargetRecovery) == EFI_INVALID_PARAMETER);
  assert(Writes == 0 && memcmp(Misc, Before, sizeof Misc) == 0);

  /* "fastbootd" is one byte longer, so its selector budget is one byte shorter. */
  ResetMisc("");
  assert(RebootTargetBootOnceArm(Ten, RebootBootOnceTargetFastbootd) == EFI_SUCCESS);
  assert(strcmp((char *)Misc, "canoe-once:abcdefghij+fastbootd") == 0);
  ResetMisc("");
  assert(RebootTargetBootOnceArm(Eleven, RebootBootOnceTargetFastbootd) == EFI_INVALID_PARAMETER);
  assert(Writes == 0 && memcmp(Misc, Before, sizeof Misc) == 0);

  /* The private menu tag leaves fifteen selector bytes. */
  ResetMisc("");
  assert(RebootTargetBootOnceArm(Fifteen, RebootBootOnceTargetMenu) == EFI_SUCCESS);
  assert(strcmp((char *)Misc, "canoe-once:123456789012345+menu") == 0);
  ResetMisc("");
  assert(RebootTargetBootOnceArm(Sixteen, RebootBootOnceTargetMenu) == EFI_INVALID_PARAMETER);
  assert(Writes == 0 && memcmp(Misc, Before, sizeof Misc) == 0);

  /* The plain record keeps its full budget, and bad input writes nothing. */
  ResetMisc("");
  assert(RebootTargetBootOnceArm(TwentyOne, RebootBootOnceTargetNone) == EFI_INVALID_PARAMETER && Writes == 0);
  assert(RebootTargetBootOnceArm("bad/selector", RebootBootOnceTargetNone) == EFI_INVALID_PARAMETER && Writes == 0);
  assert(RebootTargetBootOnceArm("android", (REBOOT_BOOT_ONCE_TARGET)99) == EFI_INVALID_PARAMETER && Writes == 0);

  /* A tag is only for the managed row, so arming it elsewhere writes nothing. */
  ResetMisc("");
  assert(SfbBootOnceArm("bls:pmos", RebootBootOnceTargetRecovery) == EFI_INVALID_PARAMETER && Writes == 0);
  assert(SfbBootOnceArm("service", RebootBootOnceTargetRecovery) == EFI_INVALID_PARAMETER && Writes == 0);
  assert(SfbBootOnceArm("missing", RebootBootOnceTargetNone) == EFI_NOT_FOUND && Writes == 0);
}

static void TestReadAndTaggedConsume (void) {
  CHAR8 Selector[REBOOT_BOOT_ONCE_SELECTOR_BYTES];
  REBOOT_BOOT_ONCE_TARGET Target;
  BOOLEAN Found;

  /* A record is read back exactly as the writer spells it. */
  ResetMisc("canoe-once:android+fastbootd");
  assert(RebootTargetBootOnceReadAndClear(Selector, &Target, &Found) == EFI_SUCCESS);
  assert(Found && Target == RebootBootOnceTargetFastbootd && strcmp(Selector, "android") == 0);
  assert(Misc[0] == 0 && Writes == 1);
  ResetMisc("canoe-once:android+menu");
  assert(RebootTargetBootOnceReadAndClear(Selector, &Target, &Found) == EFI_SUCCESS);
  assert(Found && Target == RebootBootOnceTargetMenu && strcmp(Selector, "android") == 0);
  assert(Misc[0] == 0 && Writes == 1);
  ResetMisc("canoe-once:android");
  assert(RebootTargetBootOnceReadAndClear(Selector, &Target, &Found) == EFI_SUCCESS);
  assert(Found && Target == RebootBootOnceTargetNone && strcmp(Selector, "android") == 0);

  /* An unrelated vendor command is not a record and is left alone. */
  ResetMisc("boot-recovery");
  assert(RebootTargetBootOnceReadAndClear(Selector, &Target, &Found) == EFI_SUCCESS);
  assert(!Found && Target == RebootBootOnceTargetNone && Selector[0] == '\0');
  assert(Writes == 0 && memcmp(Misc, Before, sizeof Misc) == 0);

  /* clear record, then standard command, then launch. */
  ResetMisc("canoe-once:android+recovery"); memcpy(Before, Misc, sizeof Misc);
  Event = ClearEvent = TargetWriteEvent = LaunchEvent = 0;
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceMenu);
  assert(LaunchedIndex == 0 && Writes == 2 && Flushes == 2);
  assert(ClearEvent < TargetWriteEvent && TargetWriteEvent < LaunchEvent);
  assert(strcmp((char *)Misc, "boot-recovery") == 0);
  assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeNone);
  assert(memcmp(Misc + 32, Before + 32, sizeof Misc - 32) == 0);

  ResetMisc("canoe-once:android+fastbootd");
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceMenu);
  assert(LaunchedIndex == 0 && TargetWriteEvent < LaunchEvent);
  assert(strcmp((char *)Misc, "boot-fastboot") == 0);
}

static void TestTagRefusals (void) {
  /* Only the managed Android row may spend a tag; every other row is dropped
   * with no BCB write at all, and never as a fastboot handoff. */
  ResetMisc("canoe-once:bls:pmos+recovery");
  assert(SfbBootOnceConsume(SfbBootModeHonestUnlocked) == SfbBootOnceMenu);
  assert(LaunchedIndex == SFB_NO_INDEX && LaunchEvent == 0);
  assert(Writes == 1 && TargetWriteEvent == 0 && Misc[0] == 0);
  assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeRecordDropped); assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeNone);

  ResetMisc("canoe-once:service+recovery");
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceMenu);
  assert(LaunchedIndex == SFB_NO_INDEX && TargetWriteEvent == 0 && Misc[0] == 0);
  assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeRecordDropped); assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeNone);

  ResetMisc("canoe-once:fastboot+fastbootd");
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceMenu);
  assert(LaunchedIndex == SFB_NO_INDEX && TargetWriteEvent == 0 && Misc[0] == 0);
  assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeRecordDropped); assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeNone);

  ResetMisc("canoe-once:android+recovery"); ManagedAndroid = FALSE;
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceMenu);
  assert(LaunchedIndex == SFB_NO_INDEX && TargetWriteEvent == 0 && Misc[0] == 0);
  assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeRecordDropped); assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeNone);

  /* A failed standard write stops the launch, after the record is already gone. */
  ResetMisc("canoe-once:android+recovery"); FailTargetWrite = TRUE;
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceMenu);
  assert(ClearEvent != 0 && ClearEvent < TargetWriteEvent);
  assert(LaunchedIndex == SFB_NO_INDEX && LaunchEvent == 0 && Misc[0] == 0);
  assert(Writes == 2 && Flushes == 1);
  assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeRecordDropped); assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeNone);
}

static void TestLaunchFailureNotice (void) {
  /*
   * The record is spent and its standard reboot-target command is written and
   * flushed, so nothing here clears it: the next boot follows that target and the
   * menu notice has to say so rather than promise normal boot policy.
   */
  ResetMisc("canoe-once:android+recovery"); FailLaunch = TRUE;
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceMenu);
  assert(LaunchedIndex == 0 && TargetWriteEvent < LaunchEvent);
  assert(strcmp((char *)Misc, "boot-recovery") == 0);
  assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeRebootTargetPending);
  assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeNone);

  /* No standard command was named, so normal policy really is what applies. */
  ResetMisc("canoe-once:android"); FailLaunch = TRUE;
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceMenu);
  assert(Writes == 1 && TargetWriteEvent == 0);
  assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeRecordDropped);
  assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeNone);

  /* A standard command that could not be written leaves nothing pending either. */
  ResetMisc("canoe-once:android+recovery"); FailLaunch = TRUE; FailTargetWrite = TRUE;
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceMenu);
  assert(LaunchEvent == 0 && Misc[0] == 0);
  assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeRecordDropped);
  assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeNone);

  /* A consume that reaches its launch leaves no notice at all. */
  ResetMisc("canoe-once:android+recovery");
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceMenu);
  assert(LaunchedIndex == 0 && strcmp((char *)Misc, "boot-recovery") == 0);
  assert(SfbBootOnceTakeNotice() == SfbBootOnceNoticeNone);
}

static void TestMalformedRecords (void) {
  CHAR8 Selector[REBOOT_BOOT_ONCE_SELECTOR_BYTES];
  REBOOT_BOOT_ONCE_TARGET Target;
  BOOLEAN Found;

  ResetMisc("canoe-once:android+unknown");
  assert(RebootTargetBootOnceReadAndClear(Selector, &Target, &Found) == EFI_COMPROMISED_DATA);
  assert(!Found && Target == RebootBootOnceTargetNone && Selector[0] == '\0');
  assert(Writes == 1 && Misc[0] == 0);

  ResetMisc("canoe-once:android+");
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceNone);
  assert(Misc[0] == 0 && Writes == 1);

  ResetMisc("canoe-once:+recovery");
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceNone);
  assert(Misc[0] == 0 && Writes == 1);

  /* A selector that does not fit beside its tag is malformed, not truncated. */
  ResetMisc("canoe-once:123456789012+recovery");
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceNone);
  assert(Misc[0] == 0 && Writes == 1);

  /* Neither a terminator nor a delimiter inside the command field. */
  ResetMisc("");
  memset(Misc, 'a', 32); memcpy(Misc, "canoe-once:", 11); memcpy(Before, Misc, sizeof Misc);
  assert(RebootTargetBootOnceReadAndClear(Selector, &Target, &Found) == EFI_COMPROMISED_DATA);
  assert(!Found && Misc[0] == 0 && Writes == 1);
}

static void TestClearAndFailure (void) {
  /* Both record forms live in one namespace, so one clear covers both, and an
   * unrelated vendor command is never touched. */
  ResetMisc("canoe-once:android+recovery");
  assert(RebootTargetBootOnceClear() == EFI_SUCCESS && Misc[0] == 0 && Writes == 1);
  ResetMisc("canoe-once:android");
  assert(RebootTargetBootOnceClear() == EFI_SUCCESS && Misc[0] == 0 && Writes == 1);
  ResetMisc("vendor-command");
  assert(RebootTargetBootOnceClear() == EFI_SUCCESS && Writes == 0);
  assert(memcmp(Misc, Before, sizeof Misc) == 0);
  ResetMisc("boot-recovery");
  assert(RebootTargetBootOnceClear() == EFI_SUCCESS && Writes == 0);
  assert(memcmp(Misc, Before, sizeof Misc) == 0);

  /* A record whose clear fails is not consumed and cannot launch. */
  ResetMisc(""); assert(SfbBootOnceArm("android", RebootBootOnceTargetNone) == EFI_SUCCESS);
  memcpy(Before, Misc, sizeof Misc);
  Event = ClearEvent = TargetWriteEvent = LaunchEvent = 0; LaunchedIndex = SFB_NO_INDEX;
  FlushError = EFI_DEVICE_ERROR;
  assert(SfbBootOnceConsume(SfbBootModeAblFakeLocked) == SfbBootOnceNone);
  assert(LaunchedIndex == SFB_NO_INDEX && LaunchEvent == 0 && TargetWriteEvent == 0);
}

int main (void) {
  Bs.LocateHandleBuffer = Locate; Bs.HandleProtocol = Handle;
  Media.BlockSize = sizeof Misc; Media.IoAlign = 4096; Io.Media = &Media;
  Io.ReadBlocks = Read; Io.WriteBlocks = Write; Io.FlushBlocks = Flush;
  TestOemParser(); TestDirectLaunch();
  TestPlainRecordCompat(); TestArmBudgets(); TestReadAndTaggedConsume();
  TestTagRefusals(); TestLaunchFailureNotice(); TestMalformedRecords();
  TestClearAndFailure();
  puts("boot once: OEM parser, direct/default/session-mode launch, plain and tagged records, selector budgets, managed-row policy, ordering and failure refusal passed");
  return 0;
}
