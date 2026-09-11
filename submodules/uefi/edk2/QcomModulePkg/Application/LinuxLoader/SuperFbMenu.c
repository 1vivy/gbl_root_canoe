/*
 * Console UI for the super-fastboot boot menu.
 *
 * Three keys drive everything: volume up and volume down move the cursor, and
 * power confirms.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbMenu.h"
#include "SuperFbLaunchPolicy.h"
#include "SuperFbContainer.h"
#include "SuperFbLastBoot.h"
#include "SuperFbConfigStore.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/ShutdownServices.h>
#include <Library/RebootTargetLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextInEx.h>

/* Keeps the translation unit legal when the feature is compiled out. */
CONST CHAR8 *gSfbMenuModuleTag = "SuperFbMenu";

#define SFB_ATTR_NORMAL    EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK)
/*
 * Project credit line shown under the boot-menu title. The version is the
 * build-stamped SFB_BDS_VERSION, which is the same value the host reads back
 * as the `canoe-bds` fastboot variable, so the screen and the wire can never
 * disagree. SFB_BDS_VERSION is a narrow literal because fastboot publishes
 * CHAR8; widening it here keeps the credit one compile-time literal.
 */
#define SFB_WIDEN_(x)    L##x
#define SFB_WIDEN(x)     SFB_WIDEN_ (x)
#define SFB_MENU_CREDIT  L"gbl_root_canoe " SFB_WIDEN (SFB_BDS_VERSION) L" by 1vivy"
#define SFB_ATTR_SELECTED  EFI_TEXT_ATTR (EFI_BLACK, EFI_LIGHTGRAY)
#define SFB_ATTR_TITLE     EFI_TEXT_ATTR (EFI_WHITE, EFI_BLACK)

/* Room for the "[E] " removable-media prefix in a formatted row. */
#define SFB_ROW_PREFIX_CHARS  4

/*
 * One physical Power press may arrive as several key events. Delay only
 * completed select actions, then discard their queued duplicates before the
 * next BDS screen can interpret them as another action.
 */
#define SFB_SELECT_DEBOUNCE_US  500000

STATIC SFB_KEY mSfbPendingVolumeKey = SfbKeyTimeout;

STATIC SFB_KEY
SfbDecodeMenuKey (IN CONST EFI_INPUT_KEY *Key)
{
  if (Key->ScanCode == SCAN_UP) { return SfbKeyUp; }
  if (Key->ScanCode == SCAN_DOWN) { return SfbKeyDown; }
  /* Qualcomm ButtonsLib reports Power as SCAN_SUSPEND. Keyboard Enter uses
   * CR/LF. Decode both here so every menu uses the same select action. */
  return Key->ScanCode == SCAN_SUSPEND ||
         Key->UnicodeChar == L'\r' || Key->UnicodeChar == L'\n'
           ? SfbKeySelect : SfbKeyCancel;
}

/*
 * The one key wait in the loader.
 *
 * There used to be two: this, and a near-identical timer-event loop in
 * LinuxLoader.c for the power-on volume-key scan. They agreed on the hard part
 * — create a relative timer, wait on it alongside ConIn->WaitForKey, read the
 * stroke — and differed only in two policy decisions, which are now the two
 * parameters. A key-handling bug had to be fixed twice, and the menu is the
 * only way into this loader at all.
 *
 * FlushFirst drains anything already queued before waiting. The power-on scan
 * needs it, because a key held while the device was switched on is sitting in
 * the buffer and would answer the scan instantly. The menu must NOT do it: a
 * keypress that arrives between the redraw and this call is a real press.
 *
 * Policy decides what a non-volume key means. SfbKeyPolicyConfirm accepts
 * Power/Enter and lets other keys cancel a countdown without selecting a row.
 * SfbKeyPolicyUpOnly skips every key except volume-up, while
 * SfbKeyPolicyVolume accepts either volume key and skips power. The latter two
 * policies keep the power key used to switch the device on from being mistaken
 * for input or masking a volume key behind it.
 */
SFB_KEY
SfbWaitForKeyEx (IN UINT32          TimeoutMs,
                 IN BOOLEAN         FlushFirst,
                 IN SFB_KEY_POLICY  Policy)
{
  EFI_STATUS     Status;
  EFI_EVENT      TimerEvent = NULL;
  EFI_EVENT      WaitList[2];
  UINTN          WaitCount;
  UINTN          EventIndex;
  EFI_INPUT_KEY  Key;
  SFB_KEY        Result = SfbKeyTimeout;

  if (FlushFirst) {
    mSfbPendingVolumeKey = SfbKeyTimeout;
    gST->ConIn->Reset (gST->ConIn, FALSE);
  } else if (mSfbPendingVolumeKey != SfbKeyTimeout) {
    Result = mSfbPendingVolumeKey;
    mSfbPendingVolumeKey = SfbKeyTimeout;
    if (Policy == SfbKeyPolicyConfirm || Result == SfbKeyUp) {
      return Result;
    }
    Result = SfbKeyTimeout;
  }

  if (TimeoutMs != 0) {
    Status = gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &TimerEvent);
    if (EFI_ERROR (Status)) {
      TimerEvent = NULL;
    } else {
      /* Boot services timers count in 100ns units. */
      Status = gBS->SetTimer (TimerEvent, TimerRelative,
                              (UINT64)TimeoutMs * 10000);
      if (EFI_ERROR (Status)) {
        gBS->CloseEvent (TimerEvent);
        TimerEvent = NULL;
      }
    }
  }

  WaitList[0] = gST->ConIn->WaitForKey;
  WaitCount = 1;
  if (TimerEvent != NULL) {
    WaitList[1] = TimerEvent;
    WaitCount = 2;
  }

  while (TRUE) {
    Status = gBS->WaitForEvent (WaitCount, WaitList, &EventIndex);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "SFB: WaitForEvent failed: %r\n", Status));
      Result = SfbKeyCancel;
      break;
    }

    if (EventIndex == 1) {
      DEBUG ((EFI_D_INFO, "SFB: key wait timed out after %u ms\n", TimeoutMs));
      break;
    }

    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    if (Status == EFI_NOT_READY) { continue; }
    if (EFI_ERROR (Status)) { Result = SfbKeyCancel; break; }

    /* Startup scans accept volume keys only; menu input also accepts Power. */
    if (Key.ScanCode == SCAN_UP) {
      Result = SfbKeyUp;
      break;
    }
    if (Policy == SfbKeyPolicyUpOnly ||
        (Policy == SfbKeyPolicyVolume && Key.ScanCode != SCAN_DOWN)) {
      /* Not the key being scanned for. Keep waiting rather than reporting it:
       * the timer, not this key, decides when the scan is over. */
      DEBUG ((EFI_D_INFO, "SFB: ignoring scan=0x%x char=0x%x; still scanning\n",
              Key.ScanCode, Key.UnicodeChar));
      continue;
    }
    Result = SfbDecodeMenuKey (&Key);
    break;
  }

  if (Result == SfbKeySelect) {
    /*
     * Retain the first volume action that arrives during the debounce interval
     * while consuming duplicate select events from the same Power press.
     */
    gBS->Stall (SFB_SELECT_DEBOUNCE_US);
    while (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
      if (mSfbPendingVolumeKey != SfbKeyTimeout) {
        continue;
      }
      if (Key.ScanCode == SCAN_UP) {
        mSfbPendingVolumeKey = SfbKeyUp;
      } else if (Key.ScanCode == SCAN_DOWN) {
        mSfbPendingVolumeKey = SfbKeyDown;
      }
    }
  }

  if (TimerEvent != NULL) {
    gBS->CloseEvent (TimerEvent);
  }

  return Result;
}

SFB_KEY
SfbWaitForKey (IN UINT32 TimeoutMs)
{
  return SfbWaitForKeyEx (TimeoutMs, FALSE, SfbKeyPolicyConfirm);
}

/* ---- drawing ------------------------------------------------------------ */

/* Console-relative menu geometry only. Do not change the firmware's mode or
 * framebuffer origin. A fixed gutter keeps labels, descriptions and headings
 * aligned while the whole menu block stays centered on wider consoles. */
#define SFB_MENU_BLOCK_MAX 72
#define SFB_MENU_GUTTER 4
#define SFB_MENU_PADDING 2
STATIC UINTN mSfbMenuLeft = 4, mSfbMenuTextLeft = 10, mSfbMenuTextWidth = 64;
STATIC UINTN mSfbMenuRows = 32, mSfbMenuWidth = 72;
STATIC UINTN mSfbCountdownTop, mSfbCountdownRows;
STATIC UINTN mSfbMenuPadding = SFB_MENU_PADDING;

STATIC VOID
SfbReadMenuGeometry (VOID)
{
  UINTN Columns = 80, Rows = 32, Width, Gutter;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *Out = gST->ConOut;
  if (Out->Mode != NULL && Out->QueryMode != NULL &&
      EFI_ERROR (Out->QueryMode (Out, (UINTN)Out->Mode->Mode, &Columns, &Rows))) {
    Columns = 80; Rows = 32;
  }
  Width = Columns > 4 ? Columns - 4 : 1;
  if (Width > SFB_MENU_BLOCK_MAX) { Width = SFB_MENU_BLOCK_MAX; }
  mSfbMenuPadding = Width > 2 * SFB_MENU_PADDING + SFB_MENU_GUTTER
                     ? SFB_MENU_PADDING : 0;
  Gutter = Width > SFB_MENU_GUTTER ? SFB_MENU_GUTTER : 0;
  mSfbMenuLeft = Columns > Width ? (Columns - Width) / 2 : 0;
  mSfbMenuTextLeft = mSfbMenuLeft + mSfbMenuPadding + Gutter;
  mSfbMenuTextWidth = Width - 2 * mSfbMenuPadding - Gutter;
  mSfbMenuWidth = Width;
  mSfbMenuRows = Rows;
}

STATIC VOID
SfbMenuColumn (IN UINTN Column)
{
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *Out = gST->ConOut;
  if (Out->Mode != NULL && Out->SetCursorPosition != NULL) {
    (VOID)Out->SetCursorPosition (Out, Column, (UINTN)Out->Mode->CursorRow);
  }
}

/* Share line breaking between measurement and drawing. Long unbroken paths
 * still fit, while headings and instructions wrap at word boundaries. */
STATIC UINTN
SfbMenuLineChars (IN CONST CHAR16 *Text, IN UINTN Width)
{
  UINTN Length = 0, Space = 0;
  while (Length < Width && Text[Length] != L'\0') {
    if (Text[Length] == L' ' && Length != 0) { Space = Length; }
    Length++;
  }
  return Text[Length] != L'\0' && Text[Length] != L' ' && Space != 0 ? Space : Length;
}

STATIC UINTN
SfbMenuTextRows (IN CONST CHAR16 *Text)
{
  UINTN Rows = 0, Width = mSfbMenuWidth - 2 * mSfbMenuPadding;
  do {
    Text += SfbMenuLineChars (Text, Width);
    while (*Text == L' ') { Text++; }
    Rows++;
  } while (*Text != L'\0');
  return Rows;
}

STATIC VOID
SfbDrawWrappedMenuText (IN CONST CHAR16 *Text, IN BOOLEAN Centered)
{
  CHAR16 Line[SFB_MENU_BLOCK_MAX + 1];
  UINTN Width = Centered ? mSfbMenuWidth - 2 * mSfbMenuPadding : mSfbMenuTextWidth;
  do {
    UINTN Index, Length = SfbMenuLineChars (Text, Width);
    for (Index = 0; Index < Length; Index++) { Line[Index] = Text[Index]; }
    Line[Length] = L'\0';
    SfbMenuColumn (Centered ? mSfbMenuLeft + (mSfbMenuWidth - Length) / 2 : mSfbMenuTextLeft);
    Print (L"%s\r\n", Line);
    Text += Length;
    while (*Text == L' ') { Text++; }
  } while (*Text != L'\0');
}

VOID
SfbDrawWrappedInfo (IN CONST CHAR16 *Text)
{
  SfbDrawWrappedMenuText (Text, FALSE);
}

STATIC UINTN
SfbCopyMenuLine (OUT CHAR16 *Line, IN CONST CHAR16 *Text)
{
  UINTN Index;
  for (Index = 0; Index < mSfbMenuTextWidth && Text[Index] != L'\0'; Index++) {
    Line[Index] = Text[Index];
  }
  if (Text[Index] != L'\0' && Index >= 3) {
    Line[Index - 3] = Line[Index - 2] = Line[Index - 1] = L'.';
  }
  Line[Index] = L'\0';
  return Index;
}

VOID
SfbDrawInfoLine (IN CONST CHAR16 *Text)
{
  CHAR16 Line[SFB_MENU_BLOCK_MAX + 1];
  SfbCopyMenuLine (Line, Text);
  SfbMenuColumn (mSfbMenuTextLeft);
  Print (L"%s\r\n", Line);
}

STATIC VOID
SfbDrawMenuDivider (VOID)
{
  SfbDrawInfoLine (L"--------");
}

STATIC VOID
SfbDrawCountdown (IN UINT32 RemainingMs)
{
  CHAR16 Text[64];
  UINTN Rows;
  if (RemainingMs == 0) {
    StrCpyS (Text, ARRAY_SIZE (Text), L"Timeout is disabled.");
  } else {
    UnicodeSPrint (Text, sizeof (Text), L"Highlighted entry will boot in %us.",
                           RemainingMs / 1000 + (RemainingMs % 1000 != 0));
  }
  SfbDrawWrappedMenuText (Text, TRUE);
  /* Keep the status area the same height after the countdown is cancelled. */
  for (Rows = SfbMenuTextRows (Text); Rows < mSfbCountdownRows; Rows++) {
    Print (L"\r\n");
  }
}

VOID
SfbBeginScreen (IN CONST CHAR16 *Title, IN CONST CHAR16 *Subtitle,
                 IN CONST UINT32 *RemainingMs)
{
  SfbReadMenuGeometry ();
  mSfbCountdownRows = 0;
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"\r\n");
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_TITLE);
  SfbDrawWrappedMenuText (Title, TRUE);
  if (RemainingMs != NULL) {
    mSfbCountdownTop = gST->ConOut->Mode != NULL ? (UINTN)gST->ConOut->Mode->CursorRow : 1;
    /* UINT32 milliseconds cannot exceed 4,294,968 rounded-up seconds. */
    mSfbCountdownRows = SfbMenuTextRows (L"Highlighted entry will boot in 4294968s.");
    SfbDrawCountdown (*RemainingMs);
  }
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
  if (Subtitle != NULL) { SfbDrawWrappedMenuText (Subtitle, TRUE); }
  Print (L"\r\n");
}

BOOLEAN
SfbUpdateMenuCountdown (IN UINT32 RemainingMs)
{
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *Out = gST->ConOut;
  CHAR16 Blank[SFB_MENU_BLOCK_MAX + 1];
  UINTN Index, Column, Row, Attribute, Width = mSfbMenuWidth - 2 * mSfbMenuPadding;
  EFI_STATUS Status = EFI_SUCCESS;
  if (Out->Mode == NULL || Out->SetCursorPosition == NULL || mSfbCountdownRows == 0) { return FALSE; }
  Column = (UINTN)Out->Mode->CursorColumn;
  Row = (UINTN)Out->Mode->CursorRow;
  Attribute = (UINTN)Out->Mode->Attribute;
  for (Index = 0; Index < Width; Index++) { Blank[Index] = L' '; }
  Blank[Index] = L'\0';
  Out->SetAttribute (Out, SFB_ATTR_TITLE);
  for (Index = 0; Index < mSfbCountdownRows; Index++) {
    Status = Out->SetCursorPosition (Out, mSfbMenuLeft + mSfbMenuPadding, mSfbCountdownTop + Index);
    if (EFI_ERROR (Status)) { break; }
    Print (L"%s", Blank);
  }
  if (!EFI_ERROR (Status)) {
    Status = Out->SetCursorPosition (Out, mSfbMenuLeft + mSfbMenuPadding, mSfbCountdownTop);
    if (!EFI_ERROR (Status)) { SfbDrawCountdown (RemainingMs); }
  }
  if (EFI_ERROR (Out->SetCursorPosition (Out, Column, Row))) { Status = EFI_DEVICE_ERROR; }
  Out->SetAttribute (Out, Attribute);
  return (BOOLEAN)!EFI_ERROR (Status);
}

VOID
SfbEndScreen (IN CONST CHAR16 *Footer)
{
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
  Print (L"\r\n");
  SfbDrawWrappedMenuText (Footer, TRUE);
}

UINTN
SfbMenuRowsAvailable (IN CONST CHAR16 *Footer, IN UINTN ExtraRows)
{
  UINTN Used;
  if (gST->ConOut->Mode == NULL) { return SFB_VISIBLE_ROWS; }
  /* Current header rows, footer gap, optional more row and final console row.
   * Screens reserve their own list separators or overflow notices. */
  Used = (UINTN)gST->ConOut->Mode->CursorRow + SfbMenuTextRows (Footer) +
         3 + ExtraRows;
  return mSfbMenuRows > Used ? mSfbMenuRows - Used : 1;
}

VOID
SfbDrawRow (IN BOOLEAN Selected, IN CONST CHAR16 *Marker, IN CONST CHAR16 *Text)
{
  CHAR16 Line[SFB_MENU_BLOCK_MAX + 1];
  UINTN Length = SfbCopyMenuLine (Line, Text);
  /* Positioning precedes the highlight, so outer margins remain unselected. */
  SfbMenuColumn (mSfbMenuLeft + mSfbMenuPadding);
  gST->ConOut->SetAttribute (gST->ConOut,
                             Selected ? SFB_ATTR_SELECTED : SFB_ATTR_NORMAL);
  if (mSfbMenuTextLeft != mSfbMenuLeft + mSfbMenuPadding) {
    Print (L"%s %s ", Selected ? L">" : L" ", Marker);
  }
  while (Length < mSfbMenuTextWidth) { Line[Length++] = L' '; }
  Line[Length] = L'\0';
  Print (L"%s", Line);
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
  Print (L"\r\n");
}
STATIC
CONST CHAR16 *
SfbBootModeLabel (IN SFB_BOOT_MODE Mode)
{
  switch (Mode) {
  case SfbBootModeHonestUnlocked:
    return L"Mode 0 - Honest unlocked";
  case SfbBootModeAblFakeLocked:
    return L"Mode 1 - Android locked";
  case SfbBootModeKmProfile:
    return L"Mode 2 - Profile spoof";
  default:
    return L"Mode 1 - Android locked";
  }
}


/*
 * First row of the visible window, keeping the cursor inside it. Lists longer
 * than the window scroll rather than overflow the console.
 */
UINTN
SfbWindowStart (IN UINTN Cursor, IN UINTN Count, IN UINTN Rows)
{
  if (Count <= Rows) {
    return 0;
  }
  if (Cursor < Rows / 2) {
    return 0;
  }
  if (Cursor > Count - 1 - (Rows - Rows / 2 - 1)) {
    return Count - Rows;
  }

  return Cursor - Rows / 2;
}

VOID
SfbMoveCursor (IN OUT UINTN *Cursor, IN UINTN Count, IN SFB_KEY Key)
{
  if (Count == 0) {
    *Cursor = 0;
    return;
  }

  if (Key == SfbKeyUp) {
    *Cursor = (*Cursor == 0) ? Count - 1 : *Cursor - 1;
  } else if (Key == SfbKeyDown) {
    *Cursor = (*Cursor + 1 >= Count) ? 0 : *Cursor + 1;
  }
}

/*
 * Record a boot-progress stage.
 *
 * This used to draw the stage on the display and then dwell 120 ms, because
 * the platform flushes its own log only when a boot continues into an OS
 * stage: a fault before the menu took every mark with it, so the screen was
 * the only place a mark could survive, and without the dwell a fault
 * microseconds later left the previous screen intact and the mark invisible.
 *
 * The capture in SuperFbLog.c removed the reason for both. Marks now land in a
 * ring the BDS owns and are flushed to a file before anything that might not
 * return, so they survive a boot that dies at the menu or in fastboot, and the
 * seven stages no longer cost 120 ms each on every boot that works.
 *
 * Note that Print output is not lost either way: the platform's ConOut also
 * reaches its serial ring, so the rendered screen - menu rows, cursor and all
 * - shows up in a flushed log beside these marks. That is a bonus for reading
 * a failed boot, not a reason to draw progress twice.
 */
VOID
SfbBootMark (IN CONST CHAR16 *Stage)
{
  DEBUG ((EFI_D_INFO, "SFB: MARK stage=%s\n", Stage));
}

/* Report a failure and hold the screen until the user acknowledges it. */
VOID
SfbReportStatus (IN CONST CHAR16 *What, IN EFI_STATUS Status)
{
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
  Print (L"\r\n%s: %r\r\n", What, Status);
  Print (L"Press power to continue.\r\n");
  SfbWaitForKey (0);
}

/*
 * Hand the screen over to fastboot. The menu is the last thing that draws
 * before control leaves for the fastboot loop, which prints nothing of its own
 * until a host connects, so without this the user would be staring at a boot
 * menu that no longer responds to anything.
 */
VOID
SfbShowFastbootMode (VOID)
{
  SfbShowActionScreen (L"Super Fastboot");
}

/*
 * Clear the menu away and announce the launch. The loaded image prints nothing
 * of its own until it takes over, so without this the boot menu would linger on
 * screen through the load.
 */
STATIC BOOLEAN mSfbShowBooting = TRUE;
VOID SfbSetShowBooting (IN BOOLEAN ShowBooting) { mSfbShowBooting = ShowBooting; }

VOID
SfbShowBootingScreen (IN CONST CHAR16 *Name,
                      IN CONST CHAR16 *FilePath,
                      IN BOOLEAN       ClearScreen)
{
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_TITLE);
  /*
   * An unattended default boot must not blank whatever is already on screen
   * (typically the boot splash): only clear when the launch came from the menu,
   * where the menu itself is what needs clearing away.
   */
  if (ClearScreen) {
    gST->ConOut->ClearScreen (gST->ConOut);
  }
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);

  (VOID)FilePath;
  if (mSfbShowBooting) {
    Print (L"Booting %s\r\n", (Name != NULL && Name[0] != L'\0') ? Name : L"...");
  }

  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
}

/*
 * Announce a power action (Power Off / Restart) and leave the message on
 * screen while the reset takes effect. Neither action returns, so the screen is
 * the last thing the user sees.
 */
VOID
SfbShowActionScreen (IN CONST CHAR16 *Text)
{
  SfbBeginScreen (Text, NULL, NULL);
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);
}

/* Clear queued startup keys before the destination draws its own screen. */
VOID
SfbShowEnteringMenu (VOID)
{
  /* The selected destination draws immediately; queued startup keys do not
   * become a second menu action. No separate page or fixed startup dwell. */
  gST->ConIn->Reset (gST->ConIn, FALSE);
}

/* ---- boot menu ---------------------------------------------------------- */

typedef struct {
  SFB_MENU_TEMPLATE *Template;
  SFB_MENU_STATE     Menu;
  SFB_BOOT_MODE      CurrentMode;
  BOOLEAN            AllowCountdown;
  BOOLEAN            EnterFastboot;
  BOOLEAN            FirstRun;
} SFB_MAIN_MENU_CONTEXT;

STATIC BOOLEAN
SfbIsBootMenuLaunch (IN CONST SFB_BOOT_ENTRY *Entry)
{
  return Entry->Kind == SfbEntryEfiFile || Entry->Kind == SfbEntryBlsLinux ||
         Entry->Kind == SfbEntryBlsEfi;
}

/* Selection details stay two bounded lines, including long file paths. */
STATIC VOID SfbDrawSelectionLine (IN CONST CHAR16 *Text) { SfbDrawInfoLine (Text); }

/* Drawing is deliberately observational: use the same configured/fallback
 * choice as SfbLaunchEntry without opening files or preparing launch hooks.
 * Launch-time profile/lock-policy failures remain launch-time decisions. */
STATIC
VOID
SfbDrawMainMenuHeader (IN VOID *Context)
{
  SFB_MAIN_MENU_CONTEXT *State = (SFB_MAIN_MENU_CONTEXT *)Context;
  CONST SFB_BOOT_ENTRY *Entry;
  CONST CHAR16 *Detail;

  if (State->Template->Cursor >= State->Menu.Count) {
    return;
  }
  Entry = &State->Menu.Entry[State->Template->Cursor];
  if (SfbIsManagedAblEntry (Entry)) {
    SFB_BOOT_MODE Mode = Entry->ModeFromConfig ? Entry->Mode : State->CurrentMode;
    SfbDrawSelectionLine (SfbBootModeLabel (Mode));
    SfbDrawSelectionLine (Entry->Path);
    Print (L"\r\n");
    return;
  }
  switch (Entry->Kind) {
  case SfbEntryEfiFile:
    Detail = Entry->IsUsb ? L"USB EFI application" : L"EFI application";
    break;
  case SfbEntryBlsLinux:
    Detail = L"BLS Linux entry";
    break;
  case SfbEntryBlsEfi:
    Detail = L"BLS EFI entry";
    break;
  case SfbEntryMode:
    SfbDrawSelectionLine (L"Fallback for unconfigured managed loaders");
    SfbDrawSelectionLine (SfbBootModeLabel (State->CurrentMode));
    Print (L"\r\n");
    return;
  case SfbEntryFastboot:
    Detail = L"Connect to a host for device maintenance.";
    break;
  case SfbEntrySetupFastboot:
    Detail = L"No boot root yet. Enter Super Fastboot to set up.";
    break;
  case SfbEntrySelector:
    Detail = L"Browse and launch an EFI application.";
    break;
  case SfbEntryTools:
    Detail = L"Browse the installed EFI tools.";
    break;
  case SfbEntrySaveDefault:
    Detail = L"Choose the entry used on future boots.";
    break;
  case SfbEntryMassStorage:
    Detail = L"Choose storage to share with a host over USB.";
    break;
  case SfbEntryAdvanced:
    Detail = L"Defaults, Android modes, boot policy and EFI tools.";
    break;
  case SfbEntryReboot:
    Detail = L"Restart into Fastbootd, bootloader, recovery or system.";
    break;
  case SfbEntryRecovery:
    Detail = L"Restart the phone into recovery.";
    break;
  case SfbEntryRestart:
    Detail = L"Restart the phone.";
    break;
  case SfbEntryPowerOff:
    Detail = L"Turn off the phone.";
    break;
  default:
    Detail = Entry->Desc;
    break;
  }
  if (Entry->Kind == SfbEntryEfiFile || Entry->Kind == SfbEntryBlsLinux ||
      Entry->Kind == SfbEntryBlsEfi) {
    SfbDrawSelectionLine (Detail);
    SfbDrawSelectionLine (Entry->Path);
  } else {
    SfbDrawSelectionLine (Entry->Desc);
    SfbDrawSelectionLine (Detail);
  }
  Print (L"\r\n");
}

STATIC
VOID
SfbDrawMainMenuRow (IN VOID    *Context,
                    IN UINTN    Row,
                    IN BOOLEAN  Selected)
{
  SFB_MAIN_MENU_CONTEXT *State = (SFB_MAIN_MENU_CONTEXT *)Context;
  CONST SFB_BOOT_ENTRY  *Entry = &State->Menu.Entry[Row];
  CONST CHAR16          *Marker = (Row == State->Menu.DefaultIndex)
                                  ? L"*" : L" ";
  CONST CHAR16          *Prefix = Entry->IsUsb ? L"[E] " : L"";
  if (Row == 0 || Entry->Kind == SfbEntryMassStorage ||
      Entry->Kind == SfbEntryAdvanced || Entry->Kind == SfbEntryReboot ||
      Entry->Kind == SfbEntryPowerOff) {
    if (Row == 0 && Entry->Kind == SfbEntryMassStorage) { SfbDrawInfoLine (L"No boot entries."); }
    SfbDrawMenuDivider ();
  }


  if (Entry->Role != SfbConfigRoleOther || Entry->Passthrough) {
    CONST CHAR8 *AsciiSuffix = SfbConfigRoleSuffix (Entry->Role);
    CHAR16 Suffix[16];
    CHAR16 Passthrough[16];
    CHAR16 Text[SFB_DESC_CHARS + SFB_ROW_PREFIX_CHARS +
                ARRAY_SIZE (Suffix) + ARRAY_SIZE (Passthrough)];
    UINTN SuffixIndex;

    for (SuffixIndex = 0;
         SuffixIndex + 1 < ARRAY_SIZE (Suffix) &&
         AsciiSuffix[SuffixIndex] != '\0'; SuffixIndex++) {
      Suffix[SuffixIndex] = (CHAR16)(UINT8)AsciiSuffix[SuffixIndex];
    }
    Suffix[SuffixIndex] = L'\0';
    StrCpyS (Passthrough, ARRAY_SIZE (Passthrough),
             Entry->Passthrough ? L" (passthrough)" : L"");
    UnicodeSPrint (Text, sizeof (Text), L"%s%s%s%s", Prefix, Entry->Desc,
                   Suffix, Passthrough);
    SfbDrawRow (Selected, Marker, Text);
  } else if (Entry->IsUsb) {
    CHAR16 Text[SFB_DESC_CHARS + SFB_ROW_PREFIX_CHARS];

    UnicodeSPrint (Text, sizeof (Text), L"%s%s", Prefix, Entry->Desc);
    SfbDrawRow (Selected, Marker, Text);
  } else {
    SfbDrawRow (Selected, Marker, Entry->Desc);
  }
}
STATIC VOID SfbRunAdvancedMenu (IN SFB_MAIN_MENU_CONTEXT *State);
STATIC VOID SfbRunRebootMenu (VOID);

/* Rebuilding discovery after a child returns must not turn Save default into
 * a cursor jump. Match the selected entry's stable name, or its volume/path
 * when it is a manually discovered EFI; built-in actions match their kind. */
STATIC UINTN
SfbRestoreMainSelection (CONST SFB_MENU_STATE *Menu,
                         CONST SFB_BOOT_ENTRY *Previous, UINTN PreviousRow)
{
  UINTN Index;
  if (Previous == NULL) {
    return Menu->DefaultIndex < Menu->Count ? Menu->DefaultIndex : 0;
  }
  for (Index = 0; Index < Menu->Count; Index++) {
    CONST SFB_BOOT_ENTRY *Entry = &Menu->Entry[Index];
    if (Entry->Kind != Previous->Kind || Entry->IsUsb != Previous->IsUsb) { continue; }
    if (Previous->DefaultTarget[0] != '\0') {
      if (AsciiStrCmp (Entry->DefaultTarget, Previous->DefaultTarget) == 0) { return Index; }
    } else if (Entry->Kind == SfbEntryEfiFile || Entry->Kind == SfbEntryBlsLinux ||
               Entry->Kind == SfbEntryBlsEfi) {
      if (Entry->Volume == Previous->Volume && StrCmp (Entry->Path, Previous->Path) == 0) { return Index; }
    } else if (Entry->Kind != SfbEntryBack || StrCmp (Entry->Desc, Previous->Desc) == 0) {
      return Index;
    }
  }
  return PreviousRow < Menu->Count ? PreviousRow : Menu->Count ? Menu->Count - 1 : 0;
}

STATIC
EFI_STATUS
SfbRefreshMainMenu (IN VOID *Context)
{
  SFB_MAIN_MENU_CONTEXT *State = (SFB_MAIN_MENU_CONTEXT *)Context;
  SFB_BOOT_ENTRY Previous;
  UINTN PreviousRow = State->Template->Cursor;
  BOOLEAN HadSelection = PreviousRow < State->Menu.Count;

  /* Only value fields are compared after discovery; the old DevicePath is
   * owned and released by SfbFreeMenu, never dereferenced by this copy. */
  if (HadSelection) { CopyMem (&Previous, &State->Menu.Entry[PreviousRow], sizeof (Previous)); }
  SfbFreeMenu (&State->Menu);
  SfbBuildMenu (&State->Menu, State->CurrentMode, State->FirstRun);
  SfbSetShowBooting (State->Menu.ShowBooting);
  SfbSetLaunchLockPolicy (State->Menu.ConfigValid
                          ? State->Menu.LockPolicy
                          : SfbConfigLockAsNeeded);
  State->Template->RowCount = State->Menu.Count;
  State->Template->Cursor = SfbRestoreMainSelection (&State->Menu,
                             HadSelection ? &Previous : NULL, PreviousRow);
  State->Template->TimeoutMs =
    (State->AllowCountdown &&
     State->Menu.DefaultIndex < State->Menu.Count &&
     State->Template->Cursor == State->Menu.DefaultIndex &&
     ((State->FirstRun && State->Menu.Entry[State->Template->Cursor].Kind == SfbEntrySetupFastboot) ||
      (State->Menu.MenuMode == SfbConfigMenuMenu && State->Menu.DefaultFromConfig &&
       SfbIsBootMenuLaunch (&State->Menu.Entry[State->Template->Cursor]))) &&
     State->Menu.MenuTimeoutSeconds != 0)
    ? State->Menu.MenuTimeoutSeconds * 1000 : 0;
  State->AllowCountdown = FALSE;
  return EFI_SUCCESS;
}

STATIC
VOID
SfbExitMainMenu (IN VOID *Context)
{
  SFB_MAIN_MENU_CONTEXT *State = (SFB_MAIN_MENU_CONTEXT *)Context;

  SfbFreeMenu (&State->Menu);
}

STATIC
SFB_MENU_ACTION
SfbHandleMainMenuRow (IN VOID *Context,
                      IN UINTN Row,
                      IN SFB_KEY Key)
{
  SFB_MAIN_MENU_CONTEXT *State = (SFB_MAIN_MENU_CONTEXT *)Context;
  SFB_BOOT_ENTRY        *Entry;
  EFI_STATUS             Status;

  if (State->Menu.Count == 0 || Row >= State->Menu.Count) {
    return SfbMenuActionContinue;
  }
  Entry = &State->Menu.Entry[Row];

  if (SfbIsBootMenuLaunch (Entry)) {
    if (Key == SfbKeyTimeout) {
      SfbSetShowBooting (State->Menu.ShowBooting);
  SfbSetLaunchLockPolicy (State->Menu.ConfigValid
                              ? State->Menu.LockPolicy
                              : SfbConfigLockAsNeeded);
      Status = SfbLaunchEntry (Entry, FALSE, State->CurrentMode);
    } else {
      Status = SfbLaunchEntry (Entry, TRUE, State->CurrentMode);
    }
    if (EFI_ERROR (Status)) {
      SfbReportStatus (L"Boot failed", Status);
    }
    return SfbMenuActionRebuild;
  }
  if (Key == SfbKeyTimeout && Entry->Kind != SfbEntrySetupFastboot) { return SfbMenuActionContinue; }

  switch (Entry->Kind) {
  case SfbEntryFastboot:
  case SfbEntrySetupFastboot:
    State->EnterFastboot = TRUE;
    return SfbMenuActionExit;
  case SfbEntryAdvanced:
    SfbRunAdvancedMenu (State);
    return SfbMenuActionRebuild;
  case SfbEntryReboot:
    SfbRunRebootMenu ();
    return SfbMenuActionRebuild;
  case SfbEntrySelector:
    SfbRunFileBrowser (State->CurrentMode);
    return SfbMenuActionRebuild;
  case SfbEntryTools:
    SfbRunToolsBrowser (State->CurrentMode);
    return SfbMenuActionRebuild;
  case SfbEntryMassStorage:
    SfbRunMassStorageMenu ();
    return SfbMenuActionRebuild;
  case SfbEntryRecovery:
    SfbShowActionScreen (L"Rebooting to recovery...");
    RebootDevice (RECOVERY_MODE);
    return SfbMenuActionRebuild;
  case SfbEntryBack:
    return SfbMenuActionRebuild;
  case SfbEntryPowerOff:
    SfbShowActionScreen (L"Powering off...");
    ShutdownDevice ();
    return SfbMenuActionRebuild;
  case SfbEntryRestart:
    SfbShowActionScreen (L"Restarting...");
    RebootDevice (NORMAL_MODE);
    return SfbMenuActionRebuild;
  default:
    return SfbMenuActionRebuild;
  }
}

typedef struct {
  CONST SFB_BOOT_ENTRY *Entry;
  BOOLEAN ChangeMode;
} SFB_ENTRY_PREFERENCE;

STATIC SFB_MENU_ACTION
SfbHandleEntryPreference (IN VOID *Context, IN UINTN Row, IN SFB_KEY Key)
{
  SFB_ENTRY_PREFERENCE *State = Context;
  EFI_FILE_PROTOCOL *Root = NULL;
  EFI_STATUS Status;
  (VOID)Key;
  if (Row >= (State->ChangeMode ? 3u : 1u)) { return SfbMenuActionExit; }
  Status = SfbOpenVolumeRoot (State->Entry->Volume, &Root);
  if (!EFI_ERROR (Status) && Root != NULL) {
    Status = State->ChangeMode
      ? SfbStoreConfigMode (Root, State->Entry->DefaultTarget, (UINT8)Row)
      : SfbStoreConfigDefault (Root, State->Entry->DefaultTarget);
    Root->Close (Root);
  } else if (!EFI_ERROR (Status)) { Status = EFI_DEVICE_ERROR; }
  SfbReportStatus (EFI_ERROR (Status) ? L"Could not save preference" :
                   (State->ChangeMode ? L"Entry mode saved" : L"Default saved"), Status);
  return SfbMenuActionExit;
}

STATIC VOID
SfbConfirmEntryPreference (CONST SFB_BOOT_ENTRY *Entry, BOOLEAN ChangeMode)
{
  SFB_MENU_ROW Rows[] = {{NULL,L" "},{NULL,L" "},{NULL,L" "},{L"Cancel",L" "}};
  SFB_MENU_TEMPLATE Template;
  SFB_ENTRY_PREFERENCE State;
  UINTN Index;
  ZeroMem (&Template, sizeof (Template));
  State.Entry = Entry; State.ChangeMode = ChangeMode;
  if (ChangeMode) {
    for (Index = 0; Index < 3; Index++) {
      Rows[Index].Text = SfbBootModeLabel ((SFB_BOOT_MODE)Index);
      Rows[Index].Marker = Entry->Mode == Index ? L"*" : L" ";
    }
  } else { Rows[0].Text = L"Save as default"; Rows[1].Text = L"Cancel"; }
  Template.Title = Entry->Desc;
  Template.Subtitle = ChangeMode
    ? L"Mode 0 to/from 1/2 requires formatting data. Custom ROMs use Mode 2."
    : L"Use this entry on future boots; keep its mode unchanged.";
  Template.Footer = L"Saves a preference; never boots or formats the phone.";
  Template.Rows = Rows; Template.RowCount = ChangeMode ? 4 : 2;
  Template.Cursor = Template.RowCount - 1; Template.Navigate = TRUE;
  Template.Context = &State; Template.Handler = SfbHandleEntryPreference;
  (VOID)SfbRunMenu (&Template);
}

typedef struct {
  CONST SFB_MENU_STATE *Menu;
  UINTN Map[SFB_MAX_ENTRIES];
  UINTN Count;
  BOOLEAN ChangeMode;
} SFB_ENTRY_CHOICE;

STATIC SFB_MENU_ACTION
SfbHandleEntryChoice (IN VOID *Context, IN UINTN Row, IN SFB_KEY Key)
{
  SFB_ENTRY_CHOICE *State = Context;
  (VOID)Key;
  if (Row < State->Count) {
    SfbConfirmEntryPreference (&State->Menu->Entry[State->Map[Row]], State->ChangeMode);
  }
  return SfbMenuActionExit;
}

STATIC VOID
SfbRunEntryPreference (CONST SFB_MENU_STATE *Menu, BOOLEAN ChangeMode)
{
  SFB_ENTRY_CHOICE State;
  SFB_MENU_TEMPLATE Template;
  SFB_MENU_ROW Rows[SFB_MAX_ENTRIES + 1];
  UINTN Index;
  ZeroMem (&State, sizeof (State)); ZeroMem (&Template, sizeof (Template));
  State.Menu = Menu; State.ChangeMode = ChangeMode;
  for (Index = 0; Index < Menu->Count; Index++) {
    CONST SFB_BOOT_ENTRY *Entry = &Menu->Entry[Index];
    if (!SfbIsContainerVolume (Entry->Volume) || Entry->DefaultTarget[0] == '\0') { continue; }
    if (ChangeMode && (!SfbIsManagedAblEntry (Entry) || !Entry->ModeFromConfig)) { continue; }
    State.Map[State.Count] = Index;
    Rows[State.Count].Text = Entry->Desc;
    Rows[State.Count++].Marker = Index == Menu->DefaultIndex ? L"*" : L" ";
  }
  Rows[State.Count].Text = L"Back"; Rows[State.Count].Marker = L" ";
  Template.Title = ChangeMode ? L"Change an Android entry's mode" : L"Save a default entry";
  Template.Subtitle = State.Count ? L"Choose an entry." : L"No eligible boot entries.";
  Template.Footer = L"Volume Up/Down: move   Power: select";
  Template.Rows = Rows; Template.RowCount = State.Count + 1; Template.Navigate = TRUE;
  Template.Context = &State; Template.Handler = SfbHandleEntryChoice;
  (VOID)SfbRunMenu (&Template);
}

typedef struct { UINT32 *Value; CONST UINT32 *Choices; UINTN Count; } SFB_NUMBER_CHOICE;
STATIC SFB_MENU_ACTION
SfbHandleNumber (IN VOID *Context, IN UINTN Row, IN SFB_KEY Key)
{
  SFB_NUMBER_CHOICE *State = Context;
  (VOID)Key;
  if (Row < State->Count) { *State->Value = State->Choices[Row]; }
  return SfbMenuActionExit;
}
STATIC VOID
SfbChooseNumber (CONST CHAR16 *Title, UINT32 *Value,
                 CONST UINT32 *Choices, UINTN Count, CONST CHAR16 *Unit)
{
  SFB_MENU_TEMPLATE Template;
  SFB_MENU_ROW Rows[9]; CHAR16 Labels[8][32]; UINTN Index;
  SFB_NUMBER_CHOICE State;
  if (Count > 8) { return; }
  ZeroMem (&Template, sizeof (Template));
  State.Value = Value; State.Choices = Choices; State.Count = Count;
  for (Index = 0; Index < Count; Index++) {
    UnicodeSPrint (Labels[Index], sizeof (Labels[Index]), L"%u %s", Choices[Index], Unit);
    Rows[Index].Text = Labels[Index]; Rows[Index].Marker = *Value == Choices[Index] ? L"*" : L" ";
  }
  Rows[Count].Text = L"Back"; Rows[Count].Marker = L" ";
  Template.Title = Title; Template.Subtitle = L"Zero disables this wait.";
  Template.Footer = L"Volume Up/Down: move   Power: select";
  Template.Rows = Rows; Template.RowCount = Count + 1; Template.Navigate = TRUE;
  Template.Context = &State; Template.Handler = SfbHandleNumber;
  (VOID)SfbRunMenu (&Template);
}

typedef struct { SFB_CONFIG Policy; EFI_FILE_PROTOCOL *Root; } SFB_POLICY_CONTEXT;
STATIC SFB_MENU_ACTION
SfbHandlePolicy (IN VOID *Context, IN UINTN Row, IN SFB_KEY Key)
{
  STATIC CONST UINT32 KeyWindows[] = {0,300,500,1200,2000,3000,5000,10000};
  STATIC CONST UINT32 Timeouts[] = {0,3,5,10,30,60,300};
  SFB_POLICY_CONTEXT *State = Context;
  (VOID)Key;
  switch (Row) {
  case 0:
    State->Policy.MenuMode = State->Policy.MenuMode == SfbConfigMenuSilent ? SfbConfigMenuMenu : SfbConfigMenuSilent;
    break;
  case 1:
    SfbChooseNumber (L"Startup key window", &State->Policy.KeyWindowMs, KeyWindows, ARRAY_SIZE (KeyWindows), L"ms");
    break;
  case 2:
    SfbChooseNumber (L"Boot-menu timeout", &State->Policy.MenuTimeoutSeconds, Timeouts, ARRAY_SIZE (Timeouts), L"seconds");
    break;
  case 3: State->Policy.ShowBooting = !State->Policy.ShowBooting; break;
  case 4: {
    EFI_STATUS Status = SfbStoreConfigPolicy (State->Root, &State->Policy);
    SfbReportStatus (EFI_ERROR (Status) ? L"Could not save boot policy" : L"Boot policy saved", Status);
    return SfbMenuActionExit;
  }
  default: return SfbMenuActionExit;
  }
  return SfbMenuActionContinue;
}
STATIC VOID
SfbDrawPolicyRow (IN VOID *Context, IN UINTN Row, IN BOOLEAN Selected)
{
  SFB_POLICY_CONTEXT *State = Context; CHAR16 Text[96];
  switch (Row) {
  case 0: UnicodeSPrint (Text, sizeof (Text), L"Startup: %s", State->Policy.MenuMode == SfbConfigMenuSilent ? L"silent default" : L"boot menu"); break;
  case 1: UnicodeSPrint (Text, sizeof (Text), L"Key window: %u ms", State->Policy.KeyWindowMs); break;
  case 2: UnicodeSPrint (Text, sizeof (Text), L"Menu timeout: %u seconds", State->Policy.MenuTimeoutSeconds); break;
  case 3: UnicodeSPrint (Text, sizeof (Text), L"[%s] Hide Booting...", State->Policy.ShowBooting ? L" " : L"x"); break;
  case 4: StrCpyS (Text, ARRAY_SIZE (Text), L"Save boot policy"); break;
  default: StrCpyS (Text, ARRAY_SIZE (Text), L"Cancel"); break;
  }
  SfbDrawRow (Selected, L" ", Text);
}
STATIC VOID
SfbRunPolicyMenu (VOID)
{
  SFB_POLICY_CONTEXT *State;
  SFB_MENU_TEMPLATE Template; EFI_HANDLE *Volumes = NULL;
  UINTN Count = 0, Index, Size; BOOLEAN Previous; CHAR8 *Bytes;
  EFI_STATUS Status;
  State = AllocateZeroPool (sizeof (*State));
  Bytes = AllocateZeroPool (SFB_CONFIG_MAX_BYTES + 1);
  if (State == NULL || Bytes == NULL) { Status = EFI_OUT_OF_RESOURCES; goto Done; }
  Status = SfbLocateVolumes (&Volumes, &Count);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < Count; Index++) {
    if (SfbIsContainerVolume (Volumes[Index])) { Status = SfbOpenVolumeRoot (Volumes[Index], &State->Root); break; }
  }
  if (!EFI_ERROR (Status) && State->Root == NULL) { Status = EFI_DEVICE_ERROR; }
  if (EFI_ERROR (Status)) { goto Done; }
  Status = SfbReadStoredConfig (State->Root, Bytes, &Size, &State->Policy, &Previous);
  if (Status == EFI_NOT_FOUND) {
    (VOID)SfbConfigParse ("version 1\n", 10, &State->Policy);
    Status = EFI_SUCCESS;
  }
  if (EFI_ERROR (Status)) { goto Done; }
  ZeroMem (&Template, sizeof (Template));
  Template.Title = L"Boot policy";
  Template.Subtitle = L"Volume Up or Volume Down opens the boot menu during startup.";
  Template.Footer = L"Changes take effect when saved. No boot entries are changed.";
  Template.Context = State; Template.RowCount = 6; Template.Navigate = TRUE;
  Template.Handler = SfbHandlePolicy; Template.DrawRow = SfbDrawPolicyRow;
  (VOID)SfbRunMenu (&Template);
Done:
  if (EFI_ERROR (Status)) { SfbReportStatus (L"Boot policy needs an available efisp.fat", Status); }
  if (State != NULL) { if (State->Root != NULL) { State->Root->Close (State->Root); } FreePool (State); }
  if (Bytes != NULL) { FreePool (Bytes); }
  if (Volumes != NULL) { FreePool (Volumes); }
}

STATIC SFB_MENU_ACTION
SfbHandleAdvanced (IN VOID *Context, IN UINTN Row, IN SFB_KEY Key)
{
  SFB_MAIN_MENU_CONTEXT *State = Context;
  CONST SFB_MENU_STATE *Menu = &State->Menu;
  (VOID)Key;
  switch (Row) {
  case 0: SfbRunEntryPreference (Menu, FALSE); break;
  case 1: SfbRunEntryPreference (Menu, TRUE); break;
  case 2: SfbRunPolicyMenu (); break;
  case 3: SfbRunToolsBrowser (Menu->Mode); break;
  case 4: SfbRunFileBrowser (Menu->Mode); break;
  default: return SfbMenuActionExit;
  }
  /* Preference saves can change the menu's policy and entry values. Refresh
   * those values while keeping Advanced open at the action just completed. */
  (VOID)SfbRefreshMainMenu (State);
  return SfbMenuActionContinue;
}
STATIC VOID
SfbRunAdvancedMenu (IN SFB_MAIN_MENU_CONTEXT *State)
{
  STATIC SFB_MENU_ROW Rows[] = {
    {L"Save a default entry", L" "}, {L"Change an Android entry's mode", L" "},
    {L"Boot policy",L" "}, {L"Android EFI tools",L" "},
    {L"Select an EFI file",L" "}, {L"Back",L" "}
  };
  SFB_MENU_TEMPLATE Template;
  ZeroMem (&Template, sizeof (Template));
  Template.Title = L"Advanced"; Template.Subtitle = L"Manage boot preferences or launch an EFI application.";
  Template.Footer = L"Volume Up/Down: move   Power: select";
  Template.Rows = Rows; Template.RowCount = ARRAY_SIZE (Rows); Template.Navigate = TRUE;
  Template.Context = State; Template.Handler = SfbHandleAdvanced;
  (VOID)SfbRunMenu (&Template);
}
STATIC SFB_MENU_ACTION
SfbHandleReboot (IN VOID *Context, IN UINTN Row, IN SFB_KEY Key)
{
  UINT8 Reason; EFI_STATUS Status;
  (VOID)Context; (VOID)Key;
  if (Row >= RebootTargetCount) { return SfbMenuActionExit; }
  Status = RebootTargetPrepare ((REBOOT_TARGET)Row, &Reason);
  if (EFI_ERROR (Status)) { SfbReportStatus (L"Could not prepare reboot target", Status); return SfbMenuActionExit; }
  SfbShowActionScreen (L"Restarting phone...");
  RebootDevice (Reason);
  SfbReportStatus (L"Restart returned", EFI_DEVICE_ERROR);
  return SfbMenuActionExit;
}
STATIC VOID
SfbRunRebootMenu (VOID)
{
  STATIC SFB_MENU_ROW Rows[] = {
    {L"Fastbootd",L" "},{L"Bootloader",L" "},{L"Recovery",L" "},{L"System",L" "},{L"Back",L" "}
  };
  SFB_MENU_TEMPLATE Template;
  ZeroMem (&Template, sizeof (Template));
  Template.Title = L"Reboot";
  Template.Subtitle = L"Restart the phone into the selected destination.";
  Template.Footer = L"Volume Up/Down: move   Power: select";
  Template.Rows = Rows; Template.RowCount = ARRAY_SIZE (Rows); Template.Navigate = TRUE;
  Template.Handler = SfbHandleReboot;
  (VOID)SfbRunMenu (&Template);
}


BOOLEAN
SfbRunBootMenu (IN SFB_BOOT_MODE InitialMode,
                IN BOOLEAN       AllowCountdown,
                IN BOOLEAN       FirstRun)
{
  SFB_MAIN_MENU_CONTEXT State;
  SFB_MENU_TEMPLATE     Template;

  (VOID)SfbLastBootClear ();

  if (InitialMode > SfbBootModeKmProfile) {
    InitialMode = SfbBootModeAblFakeLocked;
  }

  ZeroMem (&State, sizeof (State));
  ZeroMem (&Template, sizeof (Template));
  State.Template = &Template;
  State.CurrentMode = InitialMode;
  State.AllowCountdown = AllowCountdown;
  State.FirstRun = FirstRun;
  State.Menu.DefaultIndex = SFB_NO_INDEX;

  Template.Title = L"Boot menu";
  Template.Subtitle = SFB_MENU_CREDIT;
  Template.Footer = L"Vol Up/Down: move   Power: select";
  Template.Context = &State;
  Template.Navigate = TRUE;
  Template.Refresh = SfbRefreshMainMenu;
  Template.Exit = SfbExitMainMenu;
  Template.Handler = SfbHandleMainMenuRow;
  Template.ShowCountdown = TRUE;
  Template.ExtraRows = 5;
  Template.DrawHeader = SfbDrawMainMenuHeader;
  Template.DrawRow = SfbDrawMainMenuRow;
  (VOID)SfbRunMenu (&Template);
  return State.EnterFastboot;
}
