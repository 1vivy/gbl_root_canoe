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
/* Project credit line shown under the boot-menu title. */
#define SFB_MENU_CREDIT  L"gbl_root_canoe 7.0.0-b1 by 1vivy"
#define SFB_ATTR_SELECTED  EFI_TEXT_ATTR (EFI_BLACK, EFI_LIGHTGRAY)
#define SFB_ATTR_TITLE     EFI_TEXT_ATTR (EFI_WHITE, EFI_BLACK)

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
    gST->ConIn->Reset (gST->ConIn, FALSE);
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

/*
 * An empty boot root is an installation state, not a recoverable menu state.
 * Hold this message briefly so the operator sees why fastboot follows.
 */
VOID
SfbShowFirstRunScreen (VOID)
{
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_TITLE);
  gST->ConOut->ClearScreen (gST->ConOut);
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  Print (L"First run: no boot image installed.\r\n");
  Print (L"Entering fastboot for installation...\r\n");
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
  gBS->Stall (2 * 1000 * 1000);
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

STATIC
VOID
SfbDrawMenu (IN CONST SFB_MENU_STATE *Menu,
             IN UINTN                Cursor,
             IN CONST CHAR16         *Title)
{
  UINTN  Start;
  UINTN  Index;
  UINTN  Last;

  SfbBeginScreen (Title, SFB_MENU_CREDIT);

  if (Menu->Count == 0) {
    Print (L"  No boot entries found.\r\n");
  }

  Start = SfbWindowStart (Cursor, Menu->Count, SFB_VISIBLE_ROWS);
  Last = Start + SFB_VISIBLE_ROWS;
  if (Last > Menu->Count) {
    Last = Menu->Count;
  }

  for (Index = Start; Index < Last; Index++) {
    CONST SFB_BOOT_ENTRY  *Entry = &Menu->Entry[Index];
    CONST CHAR16          *Marker = (Index == Menu->DefaultIndex) ? L"*" : L" ";

    /* Mode is the session fallback; an entry carrying its own configured mode
     * is deliberately unaffected by this selector. */
    if (Entry->Kind == SfbEntryMode) {
      CHAR16 Text[SFB_DESC_CHARS + 90];

      UnicodeSPrint (Text, sizeof (Text),
                     L"Session mode: %s (configured entry modes unaffected)",
                     SfbBootModeLabel (Menu->Mode));
      SfbDrawRow ((BOOLEAN)(Index == Cursor), Marker, Text);
    } else if (Entry->Kind == SfbEntrySubmenu) {
      CHAR16 Text[SFB_DESC_CHARS + 4];

      UnicodeSPrint (Text, sizeof (Text), L"%s >", Entry->Desc);
      SfbDrawRow ((BOOLEAN)(Index == Cursor), Marker, Text);
    } else if (Entry->Role != SfbConfigRoleOther) {
      CONST CHAR8 *AsciiSuffix = SfbConfigRoleSuffix (Entry->Role);
      CHAR16 Suffix[16];
      CHAR16 Text[SFB_DESC_CHARS + ARRAY_SIZE (Suffix)];
      UINTN SuffixIndex;

      for (SuffixIndex = 0;
           SuffixIndex + 1 < ARRAY_SIZE (Suffix) &&
           AsciiSuffix[SuffixIndex] != '\0'; SuffixIndex++) {
        Suffix[SuffixIndex] = (CHAR16)(UINT8)AsciiSuffix[SuffixIndex];
      }
      Suffix[SuffixIndex] = L'\0';
      UnicodeSPrint (Text, sizeof (Text), L"%s%s", Entry->Desc, Suffix);
      SfbDrawRow ((BOOLEAN)(Index == Cursor), Marker, Text);
    } else {
      SfbDrawRow ((BOOLEAN)(Index == Cursor), Marker, Entry->Desc);
    }
  }

  if (Last < Menu->Count) {
    Print (L"    ... %u more\r\n", (UINT32)(Menu->Count - Last));
  }

  SfbEndScreen (L"Vol Up/Down: move   Power: select");
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
  STATIC CONST CHAR16 *Rows[] = {
    L"Mode 0 - Honest unlocked",
    L"Mode 1 - ABL fake locked",
    L"Mode 2 - KM/SPSS profile spoof",
    L"Back"
  };
  UINTN  Cursor = 0;
  UINTN  Index;

  if (CurrentMode == NULL) {
    return;
  }

  while (TRUE) {
    SFB_KEY  Key;

    SfbBeginScreen (L"Boot Mode",
                    L"Session fallback only; configured entry modes win.");
    for (Index = 0; Index < ARRAY_SIZE (Rows); Index++) {
      SfbDrawRow ((BOOLEAN)(Index == Cursor), L" ", Rows[Index]);
    }
    SfbEndScreen (L"Vol Up/Down: move   Power: select");

    Key = SfbWaitForKey (0);
    if (Key == SfbKeyUp || Key == SfbKeyDown) {
      SfbMoveCursor (&Cursor, ARRAY_SIZE (Rows), Key);
      continue;
    }

    if (Cursor == ARRAY_SIZE (Rows) - 1) {
      return;
    }

    *CurrentMode = (SFB_BOOT_MODE)Cursor;
    return;
  }
}


/*
 * Run a submenu defined by the ENTRIES file at EntriesPath on Volume. The file
 * is parsed exactly like the root BOOTENTRIES, and may itself contain further
 * '%' submenu rows; Depth bounds the nesting so a chain of files that points at
 * one another cannot recurse without limit. The submenu state is heap-allocated
 * (a single SFB_MENU_STATE is ~17 KB) so deep nesting stays off the call stack.
 *
 * Returns when the user picks the trailing "Back" row, or when the file could
 * not be built at all; the caller then redraws its own menu.
 */
STATIC
VOID
SfbRunSubMenu (IN EFI_HANDLE    Volume,
               IN CONST CHAR16  *EntriesPath,
               IN CONST CHAR16  *Title,
               IN UINTN         Depth,
               IN SFB_BOOT_MODE Mode)
{
  SFB_MENU_STATE  *Menu = NULL;
  UINTN           Cursor = 0;
  BOOLEAN         Rebuild = TRUE;
  SFB_KEY         Key;
  EFI_STATUS      Status;

  Menu = AllocateZeroPool (sizeof (*Menu));
  if (Menu == NULL) {
    return;
  }
  Menu->DefaultIndex = SFB_NO_INDEX;

  while (TRUE) {
    UINTN  Chosen;

    if (Rebuild) {
      SfbFreeMenu (Menu);
      Status = SfbBuildSubMenu (Menu, Volume, EntriesPath, Mode);
      if (EFI_ERROR (Status)) {
        SfbReportStatus (Title, Status);
        break;
      }
      Cursor = 0;
      Rebuild = FALSE;
    }

    SfbDrawMenu (Menu, Cursor, Title);

    /* Same input model as the root menu: volume keys move, power confirms. */
    Key = SfbWaitForKey (0);

    if (Key == SfbKeyUp || Key == SfbKeyDown) {
      SfbMoveCursor (&Cursor, Menu->Count, Key);
      continue;
    }

    if (Menu->Count == 0) {
      continue;
    }

    Chosen = Cursor;
    switch (Menu->Entry[Chosen].Kind) {
    case SfbEntryBack:
      goto done;

    case SfbEntrySubmenu:
      if (Depth >= SFB_MAX_SUBMENU_DEPTH) {
        SfbReportStatus (L"Submenu too deep", EFI_BUFFER_TOO_SMALL);
      } else {
        SfbRunSubMenu (Menu->Entry[Chosen].Volume,
                       Menu->Entry[Chosen].Path,
                       Menu->Entry[Chosen].Desc,
                       Depth + 1,
                       Mode);
      }
      /* Media may have changed while the child menu was open. */
      Rebuild = TRUE;
      break;

    case SfbEntryEfiFile:
    default:
      Status = SfbLaunchEntry (&Menu->Entry[Chosen], TRUE, Mode);
      if (EFI_ERROR (Status)) {
        SfbReportStatus (L"Boot failed", Status);
      }
      Rebuild = TRUE;
      break;
    }
  }

done:
  SfbFreeMenu (Menu);
  FreePool (Menu);
}

BOOLEAN
SfbRunBootMenu (IN SFB_BOOT_MODE InitialMode)
{
  SFB_MENU_STATE  Menu;
  SFB_BOOT_MODE   CurrentMode = InitialMode;
  UINTN           Cursor = 0;
  BOOLEAN         Rebuild = TRUE;
  BOOLEAN         FirstDraw = TRUE;
  SFB_KEY         Key;
  EFI_STATUS      Status;

  if (CurrentMode > SfbBootModeKmProfile) {
    CurrentMode = SfbBootModeAblFakeLocked;
  }

  ZeroMem (&Menu, sizeof (Menu));
  Menu.DefaultIndex = SFB_NO_INDEX;

  while (TRUE) {
    UINTN  Chosen;

    if (Rebuild) {
      SfbFreeMenu (&Menu);
      SfbBuildMenu (&Menu, CurrentMode);
      SfbSetLaunchLockPolicy (Menu.ConfigValid ? Menu.LockPolicy
                                               : SfbConfigLockAsNeeded);
      Cursor = (Menu.DefaultIndex != SFB_NO_INDEX &&
                Menu.DefaultIndex < Menu.Count) ? Menu.DefaultIndex : 0;
      Rebuild = FALSE;
    }

    SfbDrawMenu (&Menu, Cursor, L"Boot Menu");
    if (FirstDraw && Menu.DefaultFromConfig) {
      Key = (Menu.TimeoutSeconds == 0)
            ? SfbKeyTimeout : SfbWaitForKey (Menu.TimeoutSeconds * 1000);
    } else {
      Key = SfbWaitForKey (0);
    }
    FirstDraw = FALSE;

    if (Key == SfbKeyUp || Key == SfbKeyDown) {
      SfbMoveCursor (&Cursor, Menu.Count, Key);
      continue;
    }

    Chosen = Cursor;

    if (Menu.Count == 0) {
      continue;
    }

    switch (Menu.Entry[Chosen].Kind) {
    case SfbEntryFastboot:
      SfbFreeMenu (&Menu);
      return TRUE;

    case SfbEntryMode:
      SfbRunModeMenu (&CurrentMode);
      Rebuild = TRUE;
      break;

    case SfbEntrySelector:
      SfbRunFileBrowser (CurrentMode);
      Rebuild = TRUE;
      break;

    case SfbEntryMassStorage:
      SfbRunMassStorageMenu ();
      Rebuild = TRUE;
      break;

    case SfbEntryRecovery:
      SfbShowActionScreen (L"Rebooting to recovery...");
      RebootDevice (RECOVERY_MODE);
      break;

    case SfbEntrySubmenu:
      SfbRunSubMenu (Menu.Entry[Chosen].Volume,
                     Menu.Entry[Chosen].Path,
                     Menu.Entry[Chosen].Desc,
                     1,
                     CurrentMode);
      Rebuild = TRUE;
      break;

    case SfbEntryBack:
      Rebuild = TRUE;
      break;

    case SfbEntryPowerOff:
      SfbShowActionScreen (L"Powering off...");
      ShutdownDevice ();
      break;

    case SfbEntryRestart:
      SfbShowActionScreen (L"Restarting...");
      RebootDevice (NORMAL_MODE);
      break;

    case SfbEntryEfiFile:
    default:
      if (Key == SfbKeyTimeout) {
        SfbSetLaunchLockPolicy (Menu.ConfigValid ? Menu.LockPolicy
                                                 : SfbConfigLockAsNeeded);
        Status = SfbLaunchEntry (&Menu.Entry[Chosen], FALSE, CurrentMode);
      } else {
        Status = SfbLaunchEntry (&Menu.Entry[Chosen], TRUE, CurrentMode);
      }
      if (EFI_ERROR (Status)) {
        SfbReportStatus (L"Boot failed", Status);
      }
      Rebuild = TRUE;
      break;
    }
  }
}
