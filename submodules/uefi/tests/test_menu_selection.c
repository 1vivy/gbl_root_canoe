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
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbMenu.h"

EFI_SYSTEM_TABLE *gST;
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
static CONST CHAR8 *mExpectedHeader[32];
static SFB_KEY mKeys[32];
static UINTN mFrames, mNextKey, mSelectedRow;
static UINT32 mFirstTimeout;
static BOOLEAN mCheckMainHeader = TRUE;
static UINTN mColumns = 80;
static UINTN mConsoleRows = 32;
static BOOLEAN mEmitFrames;

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
  (void)This; (void)Attribute; return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeCursor (EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN Visible) { (void)This; (void)Visible; return EFI_SUCCESS; }

static EFI_STATUS EFIAPI
FakeQuery (EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Mode, UINTN *Columns, UINTN *Rows)
{
  (void)This; (void)Mode; *Columns = mColumns; *Rows = mConsoleRows; return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeClear (EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This)
{
  (void)This; mFrameBytes = 0; mFrame[0] = 0; return EFI_SUCCESS;
}

/* Keep the firmware key reader compiled but uncalled; the real runner consumes
 * the simulated key stream below. Production cursor movement and drawing stay
 * unchanged. Section GC discards hardware-only screens and their dependencies. */
static SFB_MENU_STATE mDiscoveredMenu;
static VOID TestBuildMenu (SFB_MENU_STATE *Menu, SFB_BOOT_MODE Mode) {
  (void)Mode; memcpy(Menu, &mDiscoveredMenu, sizeof *Menu);
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
  if (mNextKey == 0) { assert(TimeoutMs == mFirstTimeout); }
  else { assert(TimeoutMs == 0); }
  CONST CHAR8 *Header = strstr(mFrame, "\r\n\r\n");
  assert(Header != NULL);
  Header += 4;
  if (mCheckMainHeader) {
    if (strncmp(Header, mExpectedHeader[mNextKey], strlen(mExpectedHeader[mNextKey])) != 0) {
      fprintf(stderr, "Frame %llu expected header:\n%s\nActual:\n%s\n", mNextKey, mExpectedHeader[mNextKey], Header);
      assert(!"wrong selection details");
    }
  } else { assert(strstr(mFrame, mExpectedHeader[mNextKey]) != NULL); }
  if (mEmitFrames) { printf("FRAME %llu\n%s\n", mNextKey + 1, mFrame); }
  /* The old global/session row must never be presented as entry status. */
  assert(strstr(mFrame, "Session mode:") == NULL);
  return mKeys[mNextKey++];
}

static SFB_MENU_ACTION
SelectOnly (VOID *Context, UINTN Row, SFB_KEY Key)
{
  (void)Context;
  assert(Key == SfbKeySelect || Key == SfbKeyTimeout);
  mSelectedRow = Row;
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
  mFrames = mNextKey = mFirstTimeout = 0;
  mSelectedRow = SFB_NO_INDEX;
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
Frame (CONST CHAR8 *Header, SFB_KEY Key)
{
  mExpectedHeader[mFrames] = Header;
  mKeys[mFrames++] = Key;
}

int main (int argc, char **argv)
{
  EFI_SIMPLE_TEXT_OUTPUT_MODE OutputMode = {.Mode = 0};
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL Out = {.SetAttribute = FakeAttribute, .ClearScreen = FakeClear, .QueryMode = FakeQuery, .EnableCursor = FakeCursor, .Mode = &OutputMode};
  EFI_SYSTEM_TABLE Table = {.ConOut = &Out};
  SFB_MAIN_MENU_CONTEXT State;
  SFB_MENU_TEMPLATE Template;
  gST = &Table;
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
  Frame("Mode 1 - Android locked\r\n\\boot_b.efi\r\n\r\n", SfbKeyUp);
  Frame("Mode 2 - Profile spoof\r\n\\boot_a.efi\r\n\r\n", SfbKeyUp);
  Frame("Fallback for unconfigured managed loaders\r\nMode 0 - Honest unlocked\r\n\r\n", SfbKeyDown);
  Frame("Mode 2 - Profile spoof\r\n\\boot_a.efi\r\n\r\n", SfbKeyDown);
  Frame("Mode 1 - Android locked\r\n\\boot_b.efi\r\n\r\n", SfbKeyDown);
  Frame("Mode 0 - Honest unlocked\r\n\\boot_backup.efi\r\n\r\n", SfbKeyDown);
  Frame("BLS Linux entry\r\n\\linux.efi\r\n\r\n", SfbKeyDown);
  Frame("BLS EFI entry\r\n\\EFI\\mu.efi\r\n\r\n", SfbKeyDown);
  Frame("EFI application\r\n\\EFI\\stub.efi\r\n\r\n", SfbKeyDown);
  Frame("USB EFI application\r\n\\boot_a.efi\r\n\r\n", SfbKeyDown);
  Frame("Enter Super Fastboot\r\nConnect to a host for device maintenance.\r\n\r\n", SfbKeySelect);
  assert(SfbRunMenu(&Template) == EFI_SUCCESS);
  assert(mNextKey == mFrames && mSelectedRow == 7);

  /* Each configured policy wins over every fallback. Browsed/discovered
   * managed loaders follow the fallback; no formatting or reads run here. */
  unsigned Cases = 0;
  for (unsigned Configured = 0; Configured < 2; Configured++) {
    for (unsigned Mode = 0; Mode < 3; Mode++) {
      for (unsigned Fallback = 0; Fallback < 3; Fallback++) {
        Initialize(&State, &Template);
        State.CurrentMode = (SFB_BOOT_MODE)Fallback;
        Add(&State, SfbEntryEfiFile, L"\\boot_a.efi", L"Android A", (SFB_BOOT_MODE)Mode, Configured, FALSE);
        CONST CHAR8 *Modes[] = {"Mode 0 - Honest unlocked", "Mode 1 - Android locked", "Mode 2 - Profile spoof"};
        CHAR8 Header[128];
        snprintf(Header, sizeof Header, "%s\r\n\\boot_a.efi\r\n\r\n", Modes[Configured ? Mode : Fallback]);
        Template.TimeoutMs = mFirstTimeout = 5000;
        Frame(Header, SfbKeyTimeout);
        assert(SfbRunMenu(&Template) == EFI_SUCCESS);
        assert(mSelectedRow == 0 && mNextKey == 1);
        Cases++;
      }
    }
  }


  /* First run goes to the same destination with one wait, never a preliminary
   * ordinary key window. Every key is dispatched through the actual scaffold. */
  mCheckMainHeader = FALSE;
  for (unsigned Key = SfbKeyTimeout; Key <= SfbKeySelect; Key++) {
    mFrames = mNextKey = 0; mFirstTimeout = 2000;
    Frame("Volume Up/timeout: Super Fastboot   Volume Down: boot menu", (SFB_KEY)Key);
    assert(SfbShowFirstRunScreen() == (Key == SfbKeyDown));
    assert(mNextKey == 1);
  }
  mCheckMainHeader = TRUE;
  Initialize(&State, &Template);
  Add(&State, SfbEntryEfiFile, L"\\boot_a.efi", L"Android A", SfbBootModeAblFakeLocked, TRUE, FALSE);
  Add(&State, SfbEntryMassStorage, L"", L"USB mass storage", 0, FALSE, FALSE);
  Add(&State, SfbEntryFastboot, L"", L"Enter Super Fastboot", 0, FALSE, FALSE);
  Add(&State, SfbEntryAdvanced, L"", L"Advanced >", 0, FALSE, FALSE);
  Add(&State, SfbEntryReboot, L"", L"Reboot >", 0, FALSE, FALSE);
  Add(&State, SfbEntryPowerOff, L"", L"Power off", 0, FALSE, FALSE);
  Add(&State, SfbEntryRestart, L"", L"Restart", 0, FALSE, FALSE);
  Frame("Mode 1 - Android locked\r\n\\boot_a.efi\r\n\r\n", SfbKeyDown);
  Frame("USB mass storage\r\nChoose storage to share with a host over USB.\r\n\r\n", SfbKeyDown);
  Frame("Enter Super Fastboot\r\nConnect to a host for device maintenance.\r\n\r\n", SfbKeyDown);
  Frame("Advanced >\r\nDefaults, Android modes, boot policy and EFI tools.\r\n\r\n", SfbKeyDown);
  Frame("Reboot >\r\nRestart into Fastbootd, bootloader, recovery or system.\r\n\r\n", SfbKeySelect);
  assert(SfbRunMenu(&Template) == EFI_SUCCESS && mNextKey == 5);
  assert(strstr(mFrame,"Advanced >\r\n----------------------------------------\r\n") != NULL);

  mConsoleRows = 24; assert(SfbMainMenuVisibleRows() == 9);
  mConsoleRows = 32; assert(SfbMainMenuVisibleRows() == SFB_VISIBLE_ROWS);

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

  /* Banner policy is universal, including the old boot.efi spelling. A hidden
   * menu launch clears the menu; a hidden unattended launch keeps the splash. */
  FakeClear(&Out); Print(L"splash");
  SfbSetShowBooting(FALSE); SfbShowBootingScreen(L"Android",L"\\boot_a.efi",FALSE);
  assert(strcmp(mFrame,"splash")==0);
  SfbShowBootingScreen(L"Android",L"\\boot_a.efi",TRUE); assert(mFrameBytes==0);
  SfbSetShowBooting(TRUE); SfbShowBootingScreen(L"Android",L"\\boot.efi",FALSE);
  assert(strcmp(mFrame,"Booting Android\r\n")==0);

  /* Long names cannot wrap the header and shift every menu row on small
   * consoles. A BLS/EFI image retains its type instead of an Android policy. */
  Initialize(&State, &Template);
  mColumns = 36;
  Add(&State, SfbEntryBlsEfi, L"\\EFI\\a-very-long-directory-name\\another-subdir\\loader.efi", L"Long path", SfbBootModeKmProfile, TRUE, FALSE);
  Frame("BLS EFI entry\r\n\\EFI\\a-very-long-directory-name\\...\r\n\r\n", SfbKeySelect);
  assert(SfbRunMenu(&Template) == EFI_SUCCESS && mNextKey == 1);
  mColumns = 80;

  /* A vanished selection produces no stale policy or out-of-bounds lookup. */
  Initialize(&State, &Template);
  mFrameBytes = 0; mFrame[0] = 0;
  SfbDrawMainMenuHeader(&State);
  assert(mFrameBytes == 0);
  printf("menu selection: 11 navigation frames, %u policy/countdown cases, first-run keys, grouped actions, submenu selection, banner policy, long path and empty guard passed\n", Cases);
  return 0;
}
