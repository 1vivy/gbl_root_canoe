/* Exercise the production menu drawing and navigation without booting a child
 * or accessing storage. Input/output alone are simulated. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#undef NULL
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/SimpleTextOut.h>
#include <Protocol/SimpleTextInEx.h>
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbMenu.h"

EFI_SYSTEM_TABLE *gST;
EFI_BOOT_SERVICES *gBS;
VOID EFIAPI FreePool (VOID *Buffer) { (void)Buffer; assert(!"drawing must not free entries"); }
VOID *EFIAPI ZeroMem (VOID *Buffer, UINTN Size) { return memset(Buffer, 0, Size); }
VOID *EFIAPI CopyMem (VOID *Out, CONST VOID *In, UINTN Size) { return memcpy(Out, In, Size); }
INTN EFIAPI AsciiStrCmp (CONST CHAR8 *A, CONST CHAR8 *B) { return strcmp(A, B); }
INTN EFIAPI StrCmp (CONST CHAR16 *A, CONST CHAR16 *B) {
  while (*A && *A == *B) { A++; B++; }
  return (INTN)*A - (INTN)*B;
}
static CHAR8 mFrame[8192];
static UINTN mFrameBytes;
static UINT32 mExpectedTimeout[32];
static SFB_KEY mKeys[32];
static UINTN mFrames, mNextKey, mSelectedRow;
static SFB_KEY mSelectedKey;
static UINT32 mFirstTimeout;
static UINTN mColumns = 80;
static UINTN mConsoleRows = 32;
static BOOLEAN mEmitFrames;
static CHAR8 mScreen[64][256];
static UINTN mScreenAttributes[64][256];
static UINTN mScrolls;
static EFI_SIMPLE_TEXT_OUTPUT_MODE *mOutputMode;
static BOOLEAN mUseFirmwareKeys;
static EFI_INPUT_KEY mQueuedInput[8];
static UINTN mQueuedInputCount, mQueuedInputIndex, mSelectDebounces;

static UINTN
Format16 (CHAR16 *Out, UINTN Chars, CONST CHAR16 *Format, va_list Args)
{
  UINTN Count = 0;
  while (*Format != 0) {
    if (*Format != L'%') {
      assert(Count + 1 < Chars);
      Out[Count++] = *Format++;
      continue;
    }
    Format++;
    if (*Format == L's') {
      CONST CHAR16 *Text = va_arg(Args, CONST CHAR16 *);
      assert(Text != NULL);
      while (*Text != 0) { assert(Count + 1 < Chars); Out[Count++] = *Text++; }
    } else if (*Format == L'u') {
      CHAR8 Number[32];
      snprintf(Number, sizeof Number, "%u", va_arg(Args, UINT32));
      for (UINTN I = 0; Number[I] != 0; I++) {
        assert(Count + 1 < Chars); Out[Count++] = (CHAR16)Number[I];
      }
    } else { assert(!"unimplemented print conversion"); }
    Format++;
  }
  Out[Count] = 0;
  return Count;
}

UINTN EFIAPI
UnicodeSPrint (CHAR16 *Out, UINTN Bytes, CONST CHAR16 *Format, ...)
{
  va_list Args;
  va_start(Args, Format);
  UINTN Count = Format16(Out, Bytes / sizeof *Out, Format, Args);
  va_end(Args);
  return Count;
}

UINTN EFIAPI
Print (CONST CHAR16 *Format, ...)
{
  CHAR16 Text[4096];
  va_list Args;
  va_start(Args, Format);
  UINTN Count = Format16(Text, ARRAY_SIZE(Text), Format, Args);
  va_end(Args);
  /* Model firmware wrapping/scrolling, not merely strings printed per row. */
  for (UINTN I = 0; I < Count; I++) {
    if (Text[I] == L'\r') { mOutputMode->CursorColumn = 0; }
    else if (Text[I] == L'\n') { mOutputMode->CursorRow++; }
    else {
      assert(mOutputMode->CursorRow >= 0 && mOutputMode->CursorRow < 64);
      assert(mOutputMode->CursorColumn >= 0 && mOutputMode->CursorColumn < 256);
      mScreen[mOutputMode->CursorRow][mOutputMode->CursorColumn] = (CHAR8)Text[I];
      mScreenAttributes[mOutputMode->CursorRow][mOutputMode->CursorColumn] = (UINTN)mOutputMode->Attribute;
      if (++mOutputMode->CursorColumn >= (INT32)mColumns) {
        mOutputMode->CursorColumn = 0; mOutputMode->CursorRow++;
      }
    }
    if (mOutputMode->CursorRow >= (INT32)mConsoleRows) {
      mScrolls++; mOutputMode->CursorRow = (INT32)mConsoleRows - 1;
    }
  }
  assert(mFrameBytes + Count < sizeof mFrame);
  for (UINTN I = 0; I < Count; I++) { mFrame[mFrameBytes++] = (CHAR8)Text[I]; }
  mFrame[mFrameBytes] = 0;
  return Count;
}

RETURN_STATUS EFIAPI
__StrCpyS (CHAR16 *Out, UINTN Chars, CONST CHAR16 *In)
{
  UINTN I;
  for (I = 0; In[I] != 0; I++) { assert(I + 1 < Chars); Out[I] = In[I]; }
  Out[I] = 0;
  return RETURN_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeAttribute (EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Attribute)
{
  This->Mode->Attribute = (INT32)Attribute; return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeCursor (EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN Visible) { (void)This; (void)Visible; return EFI_SUCCESS; }

static EFI_STATUS EFIAPI
FakeQuery (EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Mode, UINTN *Columns, UINTN *Rows)
{
  (void)This; (void)Mode; *Columns = mColumns; *Rows = mConsoleRows; return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeSetCursor (EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Column, UINTN Row)
{
  assert(Column < mColumns && Row < mConsoleRows);
  This->Mode->CursorColumn = (INT32)Column; This->Mode->CursorRow = (INT32)Row;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeClear (EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This)
{
  This->Mode->CursorColumn = This->Mode->CursorRow = 0;
  memset(mScreen, ' ', sizeof mScreen);
  for (UINTN R = 0; R < ARRAY_SIZE(mScreenAttributes); R++) {
    for (UINTN C = 0; C < ARRAY_SIZE(mScreenAttributes[R]); C++) {
      mScreenAttributes[R][C] = (UINTN)This->Mode->Attribute;
    }
  }
  mFrameBytes = 0; mFrame[0] = 0; return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeCreateEvent (UINT32 Type, EFI_TPL Tpl, EFI_EVENT_NOTIFY Notify, VOID *Context, EFI_EVENT *Event)
{
  (void)Type; (void)Tpl; (void)Notify; (void)Context; (void)Event;
  return EFI_OUT_OF_RESOURCES;
}

static EFI_STATUS EFIAPI
FakeWaitFailure (UINTN Count, EFI_EVENT *Events, UINTN *Index)
{
  (void)Count; (void)Events; (void)Index; return EFI_DEVICE_ERROR;
}

static EFI_STATUS EFIAPI
FakeWaitKey (UINTN Count, EFI_EVENT *Events, UINTN *Index)
{
  (void)Count; (void)Events; *Index = 0; return EFI_SUCCESS;
}

static UINTN mReadCalls;
static EFI_STATUS EFIAPI
FakeReadKeyFailure (EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, EFI_INPUT_KEY *Key)
{
  (void)This; (void)Key;
  return mReadCalls++ == 0 ? EFI_NOT_READY : EFI_DEVICE_ERROR;
}

static EFI_STATUS EFIAPI
FakeReadQueuedKey (EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, EFI_INPUT_KEY *Key)
{
  (void)This;
  if (mQueuedInputIndex == mQueuedInputCount) { return EFI_NOT_READY; }
  *Key = mQueuedInput[mQueuedInputIndex++];
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeWaitQueuedKey (UINTN Count, EFI_EVENT *Events, UINTN *Index)
{
  (void)Count; (void)Events;
  /* A wrongly rejected confirm must fail here, not hang awaiting more input. */
  assert(mQueuedInputIndex < mQueuedInputCount);
  *Index = 0;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeSelectDebounce (UINTN Microseconds)
{
  (void)Microseconds;
  mSelectDebounces++;
  return EFI_SUCCESS;
}

/* Most decision cases supply logical keys. Producer-key regression cases route
 * through the production key reader before the same real runner dispatches.
 * Section GC discards hardware-only screens and their dependencies. */
static SFB_MENU_STATE mDiscoveredMenu;
static VOID TestBuildMenu (SFB_MENU_STATE *Menu, SFB_BOOT_MODE Mode, BOOLEAN FirstRun) {
  (void)Mode; (void)FirstRun; memcpy(Menu, &mDiscoveredMenu, sizeof *Menu);
}
static VOID TestFreeMenu (SFB_MENU_STATE *Menu) { memset(Menu, 0, sizeof *Menu); }
static VOID TestSetLockPolicy (SFB_CONFIG_LOCK_POLICY Policy) { (void)Policy; }
#define SfbBuildMenu TestBuildMenu
#define SfbFreeMenu TestFreeMenu
#define SfbSetLaunchLockPolicy TestSetLockPolicy
#define SfbWaitForKey SfbFirmwareWaitForKey
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbMenu.c"
#undef SfbWaitForKey
#undef SfbSetLaunchLockPolicy
#undef SfbFreeMenu
#undef SfbBuildMenu
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbMenuScaffold.c"

SFB_KEY
SfbWaitForKey (UINT32 TimeoutMs)
{
  assert(mNextKey < mFrames);
  assert(TimeoutMs == mExpectedTimeout[mNextKey]);
  /* Optional frames support manual/agent visual review; wording and geometry
   * are not acceptance assertions. */
  mFrameBytes = 0;
  for (UINTN Row = 0; Row < mConsoleRows; Row++) {
    UINTN Start = 0;
    UINTN End = mColumns;
    while (Start < End && mScreen[Row][Start] == ' ') { Start++; }
    while (End > Start && mScreen[Row][End - 1] == ' ') { End--; }
    for (UINTN Col = Start; Col < End; Col++) { mFrame[mFrameBytes++] = mScreen[Row][Col]; }
    mFrame[mFrameBytes++] = '\r'; mFrame[mFrameBytes++] = '\n';
  }
  mFrame[mFrameBytes] = 0;
  if (mEmitFrames) {
    printf("FRAME %llu\n%s\n", mNextKey + 1, mFrame);
    printf("GRID BEGIN %llux%llu\n", mColumns, mConsoleRows);
    for (UINTN Row = 0; Row < mConsoleRows; Row++) {
      fwrite(mScreen[Row], 1, mColumns, stdout); putchar('\n');
    }
    printf("GRID END\n");
    printf("ATTR BEGIN\n");
    for (UINTN Row = 0; Row < mConsoleRows; Row++) {
      for (UINTN Col = 0; Col < mColumns; Col++) { printf("%02x", (unsigned)mScreenAttributes[Row][Col]); }
      putchar('\n');
    }
    printf("ATTR END\n");
  }
  SFB_KEY Expected = mKeys[mNextKey++];
  if (mUseFirmwareKeys) {
    SFB_KEY Actual = SfbFirmwareWaitForKey(TimeoutMs);
    assert(Actual == Expected);
    return Actual;
  }
  return Expected;
}

static SFB_MENU_ACTION
SelectOnly (VOID *Context, UINTN Row, SFB_KEY Key)
{
  (void)Context;
  assert(Key == SfbKeySelect || Key == SfbKeyTimeout);
  mSelectedRow = Row;
  mSelectedKey = Key;
  return SfbMenuActionExit;
}

static void
Initialize (SFB_MAIN_MENU_CONTEXT *State, SFB_MENU_TEMPLATE *Template)
{
  memset(State, 0, sizeof *State);
  memset(Template, 0, sizeof *Template);
  State->Template = Template;
  State->CurrentMode = SfbBootModeHonestUnlocked;
  State->Menu.Mode = SfbBootModeHonestUnlocked;
  State->Menu.DefaultIndex = 1;
  Template->Title = L"Boot Menu";
  Template->Subtitle = SFB_MENU_CREDIT;
  Template->Footer = L"Vol Up/Down: move   Power: select";
  Template->Context = State;
  Template->DrawHeader = SfbDrawMainMenuHeader;
  Template->DrawRow = SfbDrawMainMenuRow;
  Template->Handler = SelectOnly;
  Template->Navigate = TRUE;
  Template->ExtraRows = 5;
  mFrames = mNextKey = mFirstTimeout = 0;
  mSelectedRow = SFB_NO_INDEX;
  mSelectedKey = SfbKeyTimeout;
  mScrolls = 0;
  mUseFirmwareKeys = FALSE;
  mQueuedInputCount = mQueuedInputIndex = mSelectDebounces = 0;
}

static void
Add (SFB_MAIN_MENU_CONTEXT *State, SFB_ENTRY_KIND Kind, CONST CHAR16 *Path,
     CONST CHAR16 *Title, SFB_BOOT_MODE Mode, BOOLEAN Configured, BOOLEAN Usb)
{
  SFB_BOOT_ENTRY *Entry = &State->Menu.Entry[State->Menu.Count++];
  Entry->Kind = Kind;
  StrCpyS(Entry->Path, ARRAY_SIZE(Entry->Path), Path);
  StrCpyS(Entry->Desc, ARRAY_SIZE(Entry->Desc), Title);
  Entry->Mode = Mode; Entry->ModeFromConfig = Configured; Entry->IsUsb = Usb;
  Entry->Passthrough = (BOOLEAN)((Kind == SfbEntryEfiFile || Kind == SfbEntryBlsLinux ||
                                  Kind == SfbEntryBlsEfi) && !SfbIsManagedAblEntry(Entry));
  State->Template->RowCount = State->Menu.Count;
}

static void
Frame (SFB_KEY Key)
{
  mExpectedTimeout[mFrames] = mFrames == 0 ? mFirstTimeout : 0;
  mKeys[mFrames++] = Key;
}

static void
CountdownFrame (UINT32 WaitMs, SFB_KEY Key)
{
  Frame(Key);
  mExpectedTimeout[mFrames - 1] = WaitMs;
}

static UINTN mRebuilds;
static SFB_MENU_ACTION
RebuildOnce (VOID *Context, UINTN Row, SFB_KEY Key)
{
  SFB_MAIN_MENU_CONTEXT *State = Context;
  if (mRebuilds++ == 0) {
    /* Even a refreshed policy after a child returns cannot restart this wait. */
    State->Template->TimeoutMs = 9000;
    return SfbMenuActionRebuild;
  }
  return SelectOnly(Context, Row, Key);
}

int main (int argc, char **argv)
{
  EFI_SIMPLE_TEXT_OUTPUT_MODE OutputMode = {.Mode = 0};
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL Out = {.SetAttribute = FakeAttribute, .ClearScreen = FakeClear, .QueryMode = FakeQuery, .SetCursorPosition = FakeSetCursor, .EnableCursor = FakeCursor, .Mode = &OutputMode};
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL In = {0};
  EFI_BOOT_SERVICES Services = {.CreateEvent = FakeCreateEvent, .WaitForEvent = FakeWaitFailure};
  EFI_SYSTEM_TABLE Table = {.ConOut = &Out, .ConIn = &In};
  SFB_MAIN_MENU_CONTEXT State;
  SFB_MENU_TEMPLATE Template;
  gST = &Table; gBS = &Services; mOutputMode = &OutputMode;
  (void)argv; mEmitFrames = (BOOLEAN)(argc > 1);

  Initialize(&State, &Template);
  Add(&State, SfbEntryEfiFile, L"\\boot_a.efi", L"Android A", SfbBootModeKmProfile, TRUE, FALSE);
  Add(&State, SfbEntryEfiFile, L"\\boot_b.efi", L"Android B", SfbBootModeAblFakeLocked, TRUE, FALSE);
  Add(&State, SfbEntryEfiFile, L"\\boot_backup.efi", L"Previous loader", SfbBootModeKmProfile, FALSE, FALSE);
  Add(&State, SfbEntryBlsLinux, L"\\linux.efi", L"Linux", SfbBootModeKmProfile, TRUE, FALSE);
  Add(&State, SfbEntryBlsEfi, L"\\EFI\\mu.efi", L"Mu", SfbBootModeAblFakeLocked, TRUE, FALSE);
  Add(&State, SfbEntryEfiFile, L"\\EFI\\stub.efi", L"EFI stub", SfbBootModeKmProfile, TRUE, FALSE);
  Add(&State, SfbEntryEfiFile, L"\\boot_a.efi", L"USB same-name EFI", SfbBootModeKmProfile, TRUE, TRUE);
  Add(&State, SfbEntryFastboot, L"", L"Enter Super Fastboot", SfbBootModeKmProfile, FALSE, FALSE);
  Add(&State, SfbEntryMode, L"", L"Unconfigured loader mode", SfbBootModeKmProfile, FALSE, FALSE);
  Template.Cursor = State.Menu.DefaultIndex;
  Template.TimeoutMs = mFirstTimeout = 5000;
  Frame(SfbKeyUp);
  Frame(SfbKeyUp);
  Frame(SfbKeyDown);
  Frame(SfbKeyDown);
  Frame(SfbKeyDown);
  Frame(SfbKeyDown);
  Frame(SfbKeyDown);
  Frame(SfbKeyDown);
  Frame(SfbKeyDown);
  Frame(SfbKeyDown);
  Frame(SfbKeySelect);
  assert(SfbRunMenu(&Template) == EFI_SUCCESS);
  assert(mNextKey == mFrames && mSelectedRow == 7);

  /* The title follows the shared wait budget. Intermediate ticks draw only;
   * the selected entry receives exactly one timeout when the budget ends. */
  Initialize(&State, &Template);
  Add(&State, SfbEntryEfiFile, L"\\boot_a.efi", L"Android A", 1, TRUE, FALSE);
  Add(&State, SfbEntryBlsEfi, L"\\EFI\\mu.efi", L"Mu", 0, FALSE, FALSE);
  Template.Cursor = 1; Template.TimeoutMs = 2500;
  Template.ShowCountdown = TRUE;

  CountdownFrame(1000, SfbKeyTimeout);
  CountdownFrame(1000, SfbKeyTimeout);
  CountdownFrame(500, SfbKeyTimeout);
  assert(SfbRunMenu(&Template) == EFI_SUCCESS);
  assert(mNextKey == 3 && mSelectedRow == 1 && mSelectedKey == SfbKeyTimeout);
  assert(mScrolls == 0);

  Initialize(&State, &Template);
  Add(&State, SfbEntryEfiFile, L"\\boot_a.efi", L"Android A", 1, TRUE, FALSE);
  Add(&State, SfbEntryBlsEfi, L"\\EFI\\mu.efi", L"Mu", 0, FALSE, FALSE);
  Template.Cursor = 1; Template.TimeoutMs = 5000;
  Template.ShowCountdown = TRUE;

  Template.Handler = RebuildOnce; mRebuilds = 0;
  CountdownFrame(1000, SfbKeyDown);
  CountdownFrame(0, SfbKeySelect);
  CountdownFrame(0, SfbKeySelect);
  assert(SfbRunMenu(&Template) == EFI_SUCCESS);
  assert(mNextKey == 3 && mRebuilds == 2 && mSelectedRow == 0 && mSelectedKey == SfbKeySelect);

  /* First run is the same menu, with a transient setup entry and the permanent
   * action both visible. Keys use ordinary navigation, not startup shortcuts. */
  for (unsigned Key = SfbKeyTimeout; Key <= SfbKeyCancel; Key++) {
    Initialize(&State, &Template);
    Add(&State, SfbEntrySetupFastboot, L"", L"Entering Super Fastboot", 0, FALSE, FALSE);
    Add(&State, SfbEntryFastboot, L"", L"Enter Super Fastboot", 0, FALSE, FALSE);
    Template.TimeoutMs = 3000; Template.ShowCountdown = TRUE;

    CountdownFrame(1000, (SFB_KEY)Key);
    if (Key == SfbKeyTimeout) {
      CountdownFrame(1000, SfbKeyTimeout);
      CountdownFrame(1000, SfbKeyTimeout);
    } else if (Key != SfbKeySelect) {
      CountdownFrame(0, SfbKeySelect);
    }
    assert(SfbRunMenu(&Template) == EFI_SUCCESS && mNextKey == mFrames);
    assert(mSelectedRow == (Key == SfbKeyUp || Key == SfbKeyDown ? 1 : 0));


  }
  /* The full first-run action set scrolls inside its window on a narrow
   * handset console. Exercise the real refresh/layout, not a static mockup. */
  Initialize(&State, &Template); mColumns = 40; mConsoleRows = 24;
  Add(&State, SfbEntrySetupFastboot, L"", L"Entering Super Fastboot", 0, FALSE, FALSE);
  Add(&State, SfbEntryMassStorage, L"", L"USB Mass Storage", 0, FALSE, FALSE);
  Add(&State, SfbEntryFastboot, L"", L"Enter Super Fastboot", 0, FALSE, FALSE);
  Add(&State, SfbEntryAdvanced, L"", L"Advanced >", 0, FALSE, FALSE);
  Add(&State, SfbEntryReboot, L"", L"Reboot >", 0, FALSE, FALSE);
  Add(&State, SfbEntryPowerOff, L"", L"Power off", 0, FALSE, FALSE);
  Add(&State, SfbEntryRestart, L"", L"Restart", 0, FALSE, FALSE);
  State.Menu.DefaultIndex = 0; State.Menu.MenuTimeoutSeconds = 3;
  memcpy(&mDiscoveredMenu, &State.Menu, sizeof mDiscoveredMenu);
  memset(&State.Menu, 0, sizeof State.Menu); State.FirstRun = State.AllowCountdown = TRUE;
  assert(SfbRefreshMainMenu(&State) == EFI_SUCCESS);
  Template.ShowCountdown = TRUE;
  CountdownFrame(1000, SfbKeySelect);
  assert(SfbRunMenu(&Template) == EFI_SUCCESS && mSelectedRow == 0 && mScrolls == 0);
  mColumns = 80; mConsoleRows = 32;

  /* The Qualcomm BootLib/MenuKeysDetection.c consumer routes SCAN_SUSPEND
   * to Enter_Action_Func; ButtonsLib/KeypadDxe emits that stroke for Power,
   * with UnicodeChar zero. CR is camera release in that producer; USB keyboard
   * Enter emits CR/LF. Keep all confirms working through the actual key wait,
   * not only a logical SfbKeySelect stub. */
  const EFI_INPUT_KEY ConfirmKeys[] = {
    {.ScanCode = SCAN_SUSPEND, .UnicodeChar = 0},
    {.ScanCode = SCAN_NULL, .UnicodeChar = L'\r'},
    {.ScanCode = SCAN_NULL, .UnicodeChar = L'\n'},
  };
  Services.WaitForEvent = FakeWaitQueuedKey;
  Services.Stall = FakeSelectDebounce;
  In.ReadKeyStroke = FakeReadQueuedKey;
  for (UINTN I = 0; I < ARRAY_SIZE(ConfirmKeys); I++) {
    Initialize(&State, &Template);
    Add(&State, SfbEntryFastboot, L"", L"Enter Super Fastboot", 0, FALSE, FALSE);
    mUseFirmwareKeys = TRUE;
    mQueuedInput[0] = ConfirmKeys[I];
    mQueuedInput[1] = ConfirmKeys[I]; /* a held key's queued repeat */
    mQueuedInput[2] = (EFI_INPUT_KEY){.ScanCode = I % 2 == 0 ? SCAN_UP : SCAN_DOWN};
    mQueuedInputCount = 3;
    Frame(SfbKeySelect);
    assert(SfbRunMenu(&Template) == EFI_SUCCESS);
    assert(mNextKey == 1 && mSelectedRow == 0 && mSelectedKey == SfbKeySelect);
    assert(mQueuedInputIndex == 3 && mSelectDebounces == 1);
    /* A navigation event queued after the Power repeat survives debounce. */
    assert(SfbFirmwareWaitForKey(0) == (I % 2 == 0 ? SfbKeyUp : SfbKeyDown));
  }
  mUseFirmwareKeys = FALSE;
  Services.WaitForEvent = FakeWaitFailure;

  /* Handset Power and keyboard Enter confirm; unrelated keyboard input only
   * interrupts the countdown and does not accidentally launch a row. */
  EFI_INPUT_KEY Input = {.ScanCode = SCAN_UP}; assert(SfbDecodeMenuKey(&Input) == SfbKeyUp);
  Input.ScanCode = SCAN_DOWN; assert(SfbDecodeMenuKey(&Input) == SfbKeyDown);
  Input.ScanCode = SCAN_SUSPEND; assert(SfbDecodeMenuKey(&Input) == SfbKeySelect);
  Input.ScanCode = 0; Input.UnicodeChar = L'\r'; assert(SfbDecodeMenuKey(&Input) == SfbKeySelect);
  Input.UnicodeChar = L'\n'; assert(SfbDecodeMenuKey(&Input) == SfbKeySelect);
  Input.UnicodeChar = L'x'; assert(SfbDecodeMenuKey(&Input) == SfbKeyCancel);
  Input.UnicodeChar = 0; Input.ScanCode = SCAN_ESC; assert(SfbDecodeMenuKey(&Input) == SfbKeyCancel);
  assert(SfbWaitForKeyEx(3000, FALSE, SfbKeyPolicyConfirm) == SfbKeyCancel);
  Services.WaitForEvent = FakeWaitKey; In.ReadKeyStroke = FakeReadKeyFailure;
  mReadCalls = 0;
  assert(SfbWaitForKeyEx(3000, FALSE, SfbKeyPolicyConfirm) == SfbKeyCancel && mReadCalls == 2);
  Initialize(&State, &Template);
  Add(&State, SfbEntryEfiFile, L"\\boot_a.efi", L"Android A", SfbBootModeAblFakeLocked, TRUE, FALSE);
  Add(&State, SfbEntryMassStorage, L"", L"USB Mass Storage", 0, FALSE, FALSE);
  Add(&State, SfbEntryFastboot, L"", L"Enter Super Fastboot", 0, FALSE, FALSE);
  Add(&State, SfbEntryAdvanced, L"", L"Advanced >", 0, FALSE, FALSE);
  Add(&State, SfbEntryReboot, L"", L"Reboot >", 0, FALSE, FALSE);
  Add(&State, SfbEntryPowerOff, L"", L"Power off", 0, FALSE, FALSE);
  Add(&State, SfbEntryRestart, L"", L"Restart", 0, FALSE, FALSE);
  Frame(SfbKeyDown);
  Frame(SfbKeyDown);
  Frame(SfbKeyDown);
  Frame(SfbKeyDown);
  Frame(SfbKeySelect);
  assert(SfbRunMenu(&Template) == EFI_SUCCESS && mNextKey == 5);



  /* Actual main-menu refresh keeps the selected action through changed
   * discovery/default preferences. Only the first display has a countdown. */
  Initialize(&State, &Template);
  Add(&State, SfbEntryEfiFile, L"\\boot_a.efi", L"Android A", 1, TRUE, FALSE);
  Add(&State, SfbEntryAdvanced, L"", L"Advanced >", 0, FALSE, FALSE);
  Add(&State, SfbEntryReboot, L"", L"Reboot >", 0, FALSE, FALSE);
  State.Menu.DefaultIndex = 0; State.Menu.DefaultFromConfig = TRUE;
  State.Menu.MenuMode = SfbConfigMenuMenu; State.Menu.MenuTimeoutSeconds = 5;
  memcpy(&mDiscoveredMenu, &State.Menu, sizeof mDiscoveredMenu);
  memset(&State.Menu, 0, sizeof State.Menu); State.AllowCountdown = TRUE;
  assert(SfbRefreshMainMenu(&State) == EFI_SUCCESS);
  assert(Template.Cursor == 0 && Template.TimeoutMs == 5000);
  Template.Cursor = 1;
  /* An extra entry appeared while a child owned the screen. */
  mDiscoveredMenu.Entry[3] = mDiscoveredMenu.Entry[2];
  mDiscoveredMenu.Entry[2] = mDiscoveredMenu.Entry[1];
  mDiscoveredMenu.Entry[1] = mDiscoveredMenu.Entry[0];
  StrCpyS(mDiscoveredMenu.Entry[1].Path, SFB_PATH_CHARS, L"\\boot_b.efi");
  mDiscoveredMenu.Count = 4; mDiscoveredMenu.DefaultIndex = 1;
  assert(SfbRefreshMainMenu(&State) == EFI_SUCCESS);
  assert(Template.Cursor == 2 && State.Menu.Entry[Template.Cursor].Kind == SfbEntryAdvanced);
  assert(Template.TimeoutMs == 0);
  Template.Cursor = 3;
  assert(SfbRefreshMainMenu(&State) == EFI_SUCCESS && Template.Cursor == 3);
  Template.Cursor = 0;
  assert(SfbRefreshMainMenu(&State) == EFI_SUCCESS && Template.Cursor == 0);
  memcpy(State.Menu.Entry[0].DefaultTarget, "android-a", sizeof "android-a");
  mDiscoveredMenu.Entry[1] = State.Menu.Entry[0];
  StrCpyS(mDiscoveredMenu.Entry[1].Path, SFB_PATH_CHARS, L"\\boot_backup.efi");
  mDiscoveredMenu.Entry[0].Kind = SfbEntryPowerOff;
  assert(SfbRefreshMainMenu(&State) == EFI_SUCCESS && Template.Cursor == 1);
  /* If the selected item disappeared, clamp the cursor without selecting a
   * configured default implicitly or rearming an automatic launch. */
  Template.Cursor = 3; mDiscoveredMenu.Count = 1;
  assert(SfbRefreshMainMenu(&State) == EFI_SUCCESS && Template.Cursor == 0 && Template.TimeoutMs == 0);

  /* A startup Volume Down is already interaction: opening the menu cannot
   * arm the configured five-second wait. Automatic entry still can. */
  for (SFB_KEY Key = SfbKeyTimeout; Key <= SfbKeySelect; Key++) {
    Initialize(&State, &Template);
    Add(&State, SfbEntryEfiFile, L"\\boot_a.efi", L"Android A", 1, TRUE, FALSE);
    State.Menu.DefaultIndex = 0; State.Menu.DefaultFromConfig = TRUE;
    State.Menu.MenuMode = SfbConfigMenuMenu; State.Menu.MenuTimeoutSeconds = 5;
    memcpy(&mDiscoveredMenu, &State.Menu, sizeof mDiscoveredMenu);
    memset(&State.Menu, 0, sizeof State.Menu);
    State.AllowCountdown = SfbPowerOnMenuCountdown(SfbConfigMenuMenu, Key);
    assert(SfbRefreshMainMenu(&State) == EFI_SUCCESS);
    assert(Template.TimeoutMs == (Key == SfbKeyTimeout ? 5000 : 0));
    if (Key == SfbKeyDown) {
      Template.ShowCountdown = TRUE;

      CountdownFrame(0, SfbKeySelect);
      assert(SfbRunMenu(&Template) == EFI_SUCCESS && mNextKey == 1);
    }
  }
  /* A first-run transient entry is the sole built-in countdown exception.
   * Silent/menu policy and explicit timeouts still apply to the same timer. */
  for (UINT32 Seconds = 0; Seconds <= 5; Seconds++) {
    Initialize(&State, &Template);
    Add(&State, SfbEntrySetupFastboot, L"", L"Entering Super Fastboot", 0, FALSE, FALSE);
    Add(&State, SfbEntryFastboot, L"", L"Enter Super Fastboot", 0, FALSE, FALSE);
    State.Menu.DefaultIndex = 0; State.Menu.DefaultFromConfig = FALSE;
    State.Menu.MenuMode = SfbConfigMenuSilent; State.Menu.MenuTimeoutSeconds = Seconds;
    memcpy(&mDiscoveredMenu, &State.Menu, sizeof mDiscoveredMenu);
    memset(&State.Menu, 0, sizeof State.Menu);
    State.FirstRun = State.AllowCountdown = TRUE;
    assert(SfbRefreshMainMenu(&State) == EFI_SUCCESS && Template.Cursor == 0);
    assert(Template.TimeoutMs == Seconds * 1000);
    Template.Cursor = 1;
    assert(SfbRefreshMainMenu(&State) == EFI_SUCCESS && Template.Cursor == 1 && Template.TimeoutMs == 0);
  }
  /* Neither an unresolved default nor a permanent built-in is an auto-boot row. */
  for (unsigned Missing = 0; Missing < 2; Missing++) {
    Initialize(&State, &Template);
    Add(&State, SfbEntryFastboot, L"", L"Enter Super Fastboot", 0, FALSE, FALSE);
    State.Menu.DefaultIndex = Missing ? SFB_NO_INDEX : 0;
    State.Menu.DefaultFromConfig = TRUE;
    State.Menu.MenuMode = SfbConfigMenuMenu; State.Menu.MenuTimeoutSeconds = 5;
    memcpy(&mDiscoveredMenu, &State.Menu, sizeof mDiscoveredMenu);
    memset(&State.Menu, 0, sizeof State.Menu); State.AllowCountdown = TRUE;
    assert(SfbRefreshMainMenu(&State) == EFI_SUCCESS && Template.TimeoutMs == 0);
  }

  /* Banner policy is universal, including the old boot.efi spelling. A hidden
   * menu launch clears the menu; a hidden unattended launch keeps the splash. */
  FakeClear(&Out); Print(L"splash");
  SfbSetShowBooting(FALSE); SfbShowBootingScreen(L"Android",L"\\boot_a.efi",FALSE);
  assert(mFrameBytes==6);
  SfbShowBootingScreen(L"Android",L"\\boot_a.efi",TRUE); assert(mFrameBytes==0);
  SfbSetShowBooting(TRUE); SfbShowBootingScreen(L"Android",L"\\boot.efi",FALSE);
  assert(mFrameBytes>0);

  /* Long names cannot wrap the header and shift every menu row on small
   * consoles. A BLS/EFI image retains its type instead of an Android policy. */
  Initialize(&State, &Template);
  mColumns = 36;
  Add(&State, SfbEntryBlsEfi, L"\\EFI\\a-very-long-directory-name\\another-subdir\\loader.efi", L"Long path", SfbBootModeKmProfile, TRUE, FALSE);
  Frame(SfbKeySelect);
  assert(SfbRunMenu(&Template) == EFI_SUCCESS && mNextKey == 1);
  /* Long lists must scroll within the menu, not the console. The countdown,
   * build line and footer wrap explicitly; entry labels and details clip. */
  Initialize(&State, &Template);
  mColumns = 40; mConsoleRows = 24;
  for (UINTN I = 0; I < 20; I++) {
    Add(&State, SfbEntryBlsEfi, L"\\EFI\\long-image.efi", L"A very long EFI entry description that wraps", 0, FALSE, FALSE);
  }
  Template.TimeoutMs = 3000; Template.ShowCountdown = TRUE;


  CountdownFrame(1000, SfbKeyTimeout);
  CountdownFrame(1000, SfbKeySelect);
  assert(SfbRunMenu(&Template) == EFI_SUCCESS);
  assert(mScrolls == 0);

  mColumns = 80; mConsoleRows = 32;

  /* A long browser/submenu subtitle consumes real console rows before its
   * list. The shared allowance includes wrapped footer and notices. */
  mColumns = 40; mConsoleRows = 24; mScrolls = 0;
  CHAR16 LongSubtitle[161];
  for (UINTN I = 0; I < 160; I++) { LongSubtitle[I] = L'x'; }
  LongSubtitle[160] = 0;
  SfbBeginScreen(L"Files", LongSubtitle, NULL);
  UINTN AvailableRows = SfbMenuRowsAvailable(L"Vol Up/Down: move   Power: open", 1);
  assert(AvailableRows < SFB_VISIBLE_ROWS);
  for (UINTN I = 0; I < AvailableRows; I++) { SfbDrawRow(I == 0, L"E", L"EFI tool"); }
  SfbDrawInfoLine(L"... more");
  SfbDrawInfoLine(L"Additional directory entries are not shown.");
  SfbEndScreen(L"Vol Up/Down: move   Power: open");
  assert(mScrolls == 0);
  mColumns = 80; mConsoleRows = 32; SfbReadMenuGeometry();

  /* A vanished selection produces no stale policy or out-of-bounds lookup. */
  Initialize(&State, &Template);
  mFrameBytes = 0; mFrame[0] = 0;
  SfbDrawMainMenuHeader(&State);
  assert(mFrameBytes == 0);
  puts("menu: key dispatch, countdown cancellation, selection retention, banner policy and console bounds passed");
  return 0;
}
