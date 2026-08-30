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

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/ShutdownServices.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/SimpleTextIn.h>

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
 * One physical Power press may arrive as several carriage returns. Delay only
 * completed select actions, then discard their queued duplicates before the
 * next BDS screen can interpret them as another action.
 */
#define SFB_SELECT_DEBOUNCE_US  500000

STATIC SFB_KEY mSfbPendingVolumeKey = SfbKeyTimeout;

/*
 * The one key wait in the loader.
 *
 * There used to be two: this, and a near-identical timer-event loop in
 * LinuxLoader.c for the power-on volume-up scan. They agreed on the hard part
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
 * Policy decides what a non-volume key means. SfbKeyPolicyConfirm treats it as
 * confirm, which is right on a three-key handset where there is nothing else
 * it can be. SfbKeyPolicyUpOnly skips it and keeps waiting, which is what the
 * power-on scan needs so that the power key used to switch the device on is
 * neither mistaken for input nor allowed to mask the volume key behind it.
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
      break;
    }

    if (EventIndex == 1) {
      DEBUG ((EFI_D_INFO, "SFB: key wait timed out after %u ms\n", TimeoutMs));
      break;
    }

    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    if (EFI_ERROR (Status)) {
      continue;
    }

    /* On the handset the Qualcomm keypad driver reports the volume keys as
     * SCAN_UP and SCAN_DOWN, and power arrives as a carriage return. */
    if (Key.ScanCode == SCAN_UP) {
      Result = SfbKeyUp;
      break;
    }
    if (Policy == SfbKeyPolicyUpOnly) {
      /* Not the key being scanned for. Keep waiting rather than reporting it:
       * the timer, not this key, decides when the scan is over. */
      DEBUG ((EFI_D_INFO, "SFB: ignoring scan=0x%x char=0x%x; still scanning\n",
              Key.ScanCode, Key.UnicodeChar));
      continue;
    }
    if (Key.ScanCode == SCAN_DOWN) {
      Result = SfbKeyDown;
    } else {
      DEBUG ((EFI_D_VERBOSE, "SFB: confirm key scan=0x%x char=0x%x\n",
              Key.ScanCode, Key.UnicodeChar));
      Result = SfbKeySelect;
    }
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

STATIC CONST CHAR16*
SfbGetFileName (IN CONST CHAR16 *Path)
{
  CONST CHAR16 *FileName = Path;
  while (*Path != L'\0') {
    if (*Path == L'\\') FileName = Path + 1;
    Path++;
  }
  return FileName;
}

STATIC BOOLEAN
SfbStrCaseEqual (IN CONST CHAR16 *Str1, IN CONST CHAR16 *Str2)
{
  while (*Str1 && *Str2) {
    CHAR16 c1 = (*Str1 >= L'a' && *Str1 <= L'z') ? *Str1 - 0x20 : *Str1;
    CHAR16 c2 = (*Str2 >= L'a' && *Str2 <= L'z') ? *Str2 - 0x20 : *Str2;
    if (c1 != c2) return FALSE;
    Str1++;
    Str2++;
  }
  return *Str1 == L'\0' && *Str2 == L'\0';
}

VOID
SfbBeginScreen (IN CONST CHAR16 *Title, IN CONST CHAR16 *Subtitle)
{
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_TITLE);
  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"%s\r\n", Title);
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
  if (Subtitle != NULL) {
    Print (L"%s\r\n", Subtitle);
  }
  Print (L"\r\n");
}

VOID
SfbEndScreen (IN CONST CHAR16 *Footer)
{
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
  Print (L"\r\n%s\r\n", Footer);
}

VOID
SfbDrawRow (IN BOOLEAN Selected, IN CONST CHAR16 *Marker, IN CONST CHAR16 *Text)
{
  gST->ConOut->SetAttribute (gST->ConOut,
                             Selected ? SFB_ATTR_SELECTED : SFB_ATTR_NORMAL);
  Print (L"%s %s %s", Selected ? L">" : L" ", Marker, Text);
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
    return L"Mode 1 - ABL fake locked";
  case SfbBootModeKmProfile:
    return L"Mode 2 - KM/SPSS profile spoof";
  default:
    return L"Mode 1 - ABL fake locked";
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

STATIC
VOID
SfbDefaultMenuDrawRow (IN VOID    *Context,
                       IN UINTN    Row,
                       IN BOOLEAN  Selected)
{
  SFB_MENU_TEMPLATE *Template = (SFB_MENU_TEMPLATE *)Context;
  CONST CHAR16      *Marker;

  Marker = (Template->Rows[Row].Marker != NULL)
           ? Template->Rows[Row].Marker : L" ";
  SfbDrawRow (Selected, Marker, Template->Rows[Row].Text);
}

EFI_STATUS
SfbMenuNoopEnter (IN VOID *Context)
{
  (VOID)Context;
  return EFI_SUCCESS;
}

VOID
SfbMenuNoopExit (IN VOID *Context)
{
  (VOID)Context;
}

EFI_STATUS
SfbRunMenu (IN OUT SFB_MENU_TEMPLATE *Template)
{
  EFI_STATUS Status = EFI_SUCCESS;
  BOOLEAN    FirstWait = TRUE;
  BOOLEAN    NeedRefresh = TRUE;

  if (Template == NULL || Template->Handler == NULL ||
      (Template->Rows == NULL && Template->DrawRow == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (Template->Enter != NULL) {
    Status = Template->Enter (Template->Context);
  }
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  while (TRUE) {
    SFB_KEY         Key;
    SFB_MENU_ACTION Action;

    if (NeedRefresh && Template->Refresh != NULL) {
      Status = Template->Refresh (Template->Context);
      if (EFI_ERROR (Status)) {
        break;
      }
      NeedRefresh = FALSE;
      if (Template->RowCount == 0) {
        Template->Cursor = 0;
      } else if (Template->Cursor >= Template->RowCount) {
        Template->Cursor = Template->RowCount - 1;
      }
    }

    SfbBeginScreen (Template->Title, Template->Subtitle);
    if (Template->DrawHeader != NULL) {
      Template->DrawHeader (Template->Context);
    }
    if (Template->RowCount == 0) {
      Print (L"  No entries found.\r\n");
    } else {
      UINTN Start = SfbWindowStart (Template->Cursor, Template->RowCount,
                                    SFB_VISIBLE_ROWS);
      UINTN Last = Start + SFB_VISIBLE_ROWS;
      UINTN Row;

      if (Last > Template->RowCount) {
        Last = Template->RowCount;
      }
      for (Row = Start; Row < Last; Row++) {
        if (Template->DrawRow != NULL) {
          Template->DrawRow (Template->Context, Row,
                             (BOOLEAN)(Row == Template->Cursor));
        } else {
          SfbDefaultMenuDrawRow (Template, Row,
                                 (BOOLEAN)(Row == Template->Cursor));
        }
      }
      if (Last < Template->RowCount) {
        Print (L"    ... %u more\r\n",
               (UINT32)(Template->RowCount - Last));
      }
    }
    SfbEndScreen (Template->Footer);

    Key = (FirstWait && Template->TimeoutMs != 0)
          ? SfbWaitForKey (Template->TimeoutMs) : SfbWaitForKey (0);
    FirstWait = FALSE;

    if (Template->Navigate && (Key == SfbKeyUp || Key == SfbKeyDown)) {
      SfbMoveCursor (&Template->Cursor, Template->RowCount, Key);
      continue;
    }

    Action = Template->Handler (Template->Context, Template->Cursor, Key);
    if (Action == SfbMenuActionExit) {
      break;
    }
    if (Action == SfbMenuActionRebuild) {
      NeedRefresh = TRUE;
    }
  }

Exit:
  if (Template->Exit != NULL) {
    Template->Exit (Template->Context);
  }
  return Status;
}

/*
 * Print a boot-progress stage to the console, then dwell.
 *
 * The platform only flushes its log when boot continues into an OS stage, so
 * a fault before the menu takes every DEBUG mark with it. These land on the
 * display instead, and the first screen the menu draws clears them - so they
 * cost nothing on a boot that works and name the last stage reached on one
 * that does not.
 *
 * The dwell is load-bearing, not politeness. Without it a fault microseconds
 * after the Print can leave the previous screen contents intact and the mark
 * invisible, which is exactly the "no change on screen, then dies" symptom
 * that made a crash unlocalisable.
 */
VOID
SfbBootMark (IN CONST CHAR16 *Stage)
{
  Print (L"[%s]\r\n", Stage);
  gBS->Stall (120 * 1000);
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
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_TITLE);
  gST->ConOut->ClearScreen (gST->ConOut);
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);

  Print (L"FASTBOOT MODE\r\n");

  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
}

STATIC
SFB_MENU_ACTION
SfbFirstRunMenuHandler (IN VOID *Context,
                        IN UINTN Row,
                        IN SFB_KEY Key)
{
  BOOLEAN *EnterMenu = (BOOLEAN *)Context;

  (VOID)Row;
  *EnterMenu = SfbFirstRunEntersMenu (Key);
  return SfbMenuActionExit;
}

/*
 * An empty boot root is normally an installation state. Keep fastboot as the
 * default, but give a first-time operator one explicit way to inspect the
 * discovered entries before handing the device to the host.
 */
BOOLEAN
SfbShowFirstRunScreen (VOID)
{
  STATIC SFB_MENU_ROW Rows[] = {
    { L"Enter boot menu (Volume Up)", L" " },
    { L"Enter fastboot (default)", L" " }
  };
  SFB_MENU_TEMPLATE Template;
  BOOLEAN EnterMenu = FALSE;

  ZeroMem (&Template, sizeof (Template));
  Template.Title = L"First run";
  Template.Subtitle = L"No boot image installed.";
  Template.Footer = L"Volume Up: menu   Power/timeout: fastboot";
  Template.Rows = Rows;
  Template.RowCount = ARRAY_SIZE (Rows);
  Template.Cursor = 1;
  Template.TimeoutMs = 2 * 1000;
  Template.Navigate = FALSE;
  Template.Context = &EnterMenu;
  Template.Enter = SfbMenuNoopEnter;
  Template.Exit = SfbMenuNoopExit;
  Template.Handler = SfbFirstRunMenuHandler;
  (VOID)SfbRunMenu (&Template);
  return EnterMenu;
}

/*
 * Clear the menu away and announce the launch. The loaded image prints nothing
 * of its own until it takes over, so without this the boot menu would linger on
 * screen through the load.
 */
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

  if (FilePath != NULL) {
    CONST CHAR16 *FileName = SfbGetFileName (FilePath);
    if (!SfbStrCaseEqual (FileName, L"boot.efi")) {
      Print (L"Booting %s\r\n", (Name != NULL && Name[0] != L'\0') ? Name : L"...");
    }
  } else {
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
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_TITLE);
  gST->ConOut->ClearScreen (gST->ConOut);
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);

  Print (L"%s\r\n", Text);

  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
}

/*
 * Seconds to hold on the "Entering Boot Menu" screen before the menu starts
 * taking input. Long enough that a volume key held from power-on has been
 * released, so it does not immediately move the menu cursor.
 */
#define SFB_ENTER_MENU_DELAY_S  3

VOID
SfbShowEnteringMenu (VOID)
{
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_TITLE);
  gST->ConOut->ClearScreen (gST->ConOut);
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);

  Print (L"Entering Boot Menu\r\n");

  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);

  /* Wait for the key to be released... */
  gBS->Stall (SFB_ENTER_MENU_DELAY_S * 1000 * 1000);

  /* ...then drop anything typed or held during the wait so it does not leak
   * into the menu as a spurious keypress. */
  gST->ConIn->Reset (gST->ConIn, FALSE);
}

/* ---- boot menu ---------------------------------------------------------- */

typedef struct {
  SFB_MENU_TEMPLATE *Template;
  SFB_MENU_STATE     Menu;
  SFB_BOOT_MODE      CurrentMode;
  BOOLEAN            EnterFastboot;
} SFB_MAIN_MENU_CONTEXT;

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

  if (Entry->Kind == SfbEntryMode) {
    CHAR16 Text[SFB_DESC_CHARS + 90];

    UnicodeSPrint (Text, sizeof (Text),
                   L"Session mode: %s (configured entry modes unaffected)",
                   SfbBootModeLabel (State->Menu.Mode));
    SfbDrawRow (Selected, Marker, Text);
  } else if (Entry->Role != SfbConfigRoleOther || Entry->Passthrough) {
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
STATIC
VOID
SfbRunModeMenu (IN OUT SFB_BOOT_MODE *CurrentMode);

STATIC
EFI_STATUS
SfbRefreshMainMenu (IN VOID *Context)
{
  SFB_MAIN_MENU_CONTEXT *State = (SFB_MAIN_MENU_CONTEXT *)Context;

  SfbFreeMenu (&State->Menu);
  SfbBuildMenu (&State->Menu, State->CurrentMode);
  SfbSetLaunchLockPolicy (State->Menu.ConfigValid
                          ? State->Menu.LockPolicy
                          : SfbConfigLockAsNeeded);
  State->Template->RowCount = State->Menu.Count;
  State->Template->Cursor = (State->Menu.DefaultIndex != SFB_NO_INDEX &&
                             State->Menu.DefaultIndex < State->Menu.Count)
                            ? State->Menu.DefaultIndex : 0;
  State->Template->TimeoutMs =
    (State->Menu.DefaultFromConfig && State->Menu.TimeoutSeconds != 0)
    ? State->Menu.TimeoutSeconds * 1000 : 0;
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

  if (Entry->Kind == SfbEntryEfiFile) {
    if (Key == SfbKeyTimeout) {
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

  switch (Entry->Kind) {
  case SfbEntryFastboot:
    State->EnterFastboot = TRUE;
    return SfbMenuActionExit;
  case SfbEntryMode:
    SfbRunModeMenu (&State->CurrentMode);
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

/* USB diagnostics moved out of the BDS: the UsbTools app under EFI Tools
 * owns the census screen and the host-mode attempt. */
STATIC
SFB_MENU_ACTION
SfbHandleModeMenuRow (IN VOID *Context,
                      IN UINTN Row,
                      IN SFB_KEY Key)
{
  SFB_BOOT_MODE *CurrentMode = (SFB_BOOT_MODE *)Context;

  (VOID)Key;
  if (Row < 3) {
    *CurrentMode = (SFB_BOOT_MODE)Row;
  }
  return SfbMenuActionExit;
}

/*
 * Select a session-only mode override. Nothing is written: canoe.cfg remains
 * the sole source of configured policy, and its entry modes win over this
 * fallback when the corresponding image is launched.
 */
STATIC
VOID
SfbRunModeMenu (IN OUT SFB_BOOT_MODE *CurrentMode)
{
  STATIC SFB_MENU_ROW Rows[] = {
    { L"Mode 0 - Honest unlocked", L" " },
    { L"Mode 1 - ABL fake locked", L" " },
    { L"Mode 2 - KM/SPSS profile spoof", L" " },
    { L"Back", L" " }
  };
  SFB_MENU_TEMPLATE Template;

  if (CurrentMode == NULL) {
    return;
  }
  ZeroMem (&Template, sizeof (Template));
  Template.Title = L"Boot Mode";
  Template.Subtitle = L"Session fallback only; configured entry modes win.";
  Template.Footer = L"Vol Up/Down: move   Power: select";
  Template.Rows = Rows;
  Template.RowCount = ARRAY_SIZE (Rows);
  Template.Navigate = TRUE;
  Template.Context = CurrentMode;
  Template.Enter = SfbMenuNoopEnter;
  Template.Exit = SfbMenuNoopExit;
  Template.Handler = SfbHandleModeMenuRow;
  (VOID)SfbRunMenu (&Template);
}

BOOLEAN
SfbRunBootMenu (IN SFB_BOOT_MODE InitialMode)
{
  SFB_MAIN_MENU_CONTEXT State;
  SFB_MENU_TEMPLATE     Template;

  if (InitialMode > SfbBootModeKmProfile) {
    InitialMode = SfbBootModeAblFakeLocked;
  }

  ZeroMem (&State, sizeof (State));
  ZeroMem (&Template, sizeof (Template));
  State.Template = &Template;
  State.CurrentMode = InitialMode;
  State.Menu.DefaultIndex = SFB_NO_INDEX;

  Template.Title = L"Boot Menu";
  Template.Subtitle = SFB_MENU_CREDIT;
  Template.Footer = L"Vol Up/Down: move   Power: select";
  Template.Context = &State;
  Template.Navigate = TRUE;
  Template.Enter = SfbMenuNoopEnter;
  Template.Refresh = SfbRefreshMainMenu;
  Template.Exit = SfbExitMainMenu;
  Template.Handler = SfbHandleMainMenuRow;
  Template.DrawRow = SfbDrawMainMenuRow;
  (VOID)SfbRunMenu (&Template);
  return State.EnterFastboot;
}
