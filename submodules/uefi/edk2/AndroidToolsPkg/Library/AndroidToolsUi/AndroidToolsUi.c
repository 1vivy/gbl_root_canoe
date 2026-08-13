/** @file
 *  Console menu UI for AndroidToolsPkg, ported from the super-fastboot boot
 *  menu (SuperFbMenu.c). Volume up/down move the cursor, power confirms.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextOut.h>

#include "AndroidToolsUi.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)  (sizeof (a) / sizeof ((a)[0]))
#endif

#define AT_ATTR_NORMAL    EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK)
#define AT_ATTR_SELECTED  EFI_TEXT_ATTR (EFI_BLACK, EFI_LIGHTGRAY)
#define AT_ATTR_TITLE     EFI_TEXT_ATTR (EFI_WHITE, EFI_BLACK)

/* Seconds to wait for the key that launched us to be released before the
 * input queue is drained. Mirrors SFB_ENTER_MENU_DELAY_S in SuperFbMenu.c:
 * without it a power press held through LoadImage/StartImage is read back at
 * once and confirms the first menu entry. */
#define AT_ENTER_MENU_DELAY_S  2

AT_KEY
AtUiWaitForKey (
  IN UINT32 TimeoutMs
  )
{
  EFI_STATUS     Status;
  EFI_EVENT      TimerEvent = NULL;
  EFI_EVENT      WaitList[2];
  UINTN          WaitCount;
  UINTN          EventIndex;
  EFI_INPUT_KEY  Key;
  AT_KEY         Result = AtKeyTimeout;

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
      DEBUG ((DEBUG_ERROR, "AT: WaitForEvent failed: %r\n", Status));
      break;
    }

    if (EventIndex == 1) {
      break;
    }

    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    if (EFI_ERROR (Status)) {
      continue;
    }

    /*
     * On the handset the Qualcomm keypad driver reports the volume keys as
     * SCAN_UP and SCAN_DOWN, and power arrives as a carriage return. Anything
     * left over counts as confirm.
     */
    if (Key.ScanCode == SCAN_UP) {
      Result = AtKeyUp;
    } else if (Key.ScanCode == SCAN_DOWN) {
      Result = AtKeyDown;
    } else {
      Result = AtKeySelect;
    }
    break;
  }

  if (TimerEvent != NULL) {
    gBS->CloseEvent (TimerEvent);
  }

  return Result;
}

/*
 * Announce the menu, then wait for the key that launched us to be released
 * before draining the input queue. Without this, a power press held through
 * the LoadImage/StartImage transition would be read back at once and confirm
 * the first menu entry. Mirrors SfbShowEnteringMenu in SuperFbMenu.c.
 */
VOID
AtUiEnterMenu (
  IN CONST CHAR16 *Title
  )
{
  gST->ConOut->SetAttribute (gST->ConOut, AT_ATTR_TITLE);
  gST->ConOut->ClearScreen (gST->ConOut);
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  Print (L"Entering %s\r\n", (Title != NULL) ? Title : L"Menu");
  gST->ConOut->SetAttribute (gST->ConOut, AT_ATTR_NORMAL);

  /* Wait for the launching key to be released... */
  gBS->Stall (AT_ENTER_MENU_DELAY_S * 1000 * 1000);

  /* ...then drop anything typed or held during the wait so it does not leak
   * into the menu as a spurious confirm. */
  gST->ConIn->Reset (gST->ConIn, FALSE);
}

/* ---- drawing ------------------------------------------------------------ */

VOID
AtUiBeginScreen (
  IN CONST CHAR16 *Title,
  IN CONST CHAR16 *Subtitle
  )
{
  gST->ConOut->SetAttribute (gST->ConOut, AT_ATTR_TITLE);
  gST->ConOut->ClearScreen (gST->ConOut);
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  Print (L"%s\r\n", Title);
  gST->ConOut->SetAttribute (gST->ConOut, AT_ATTR_NORMAL);
  if (Subtitle != NULL) {
    Print (L"%s\r\n", Subtitle);
  }
  Print (L"\r\n");
}

VOID
AtUiEndScreen (
  IN CONST CHAR16 *Footer
  )
{
  gST->ConOut->SetAttribute (gST->ConOut, AT_ATTR_NORMAL);
  if (Footer != NULL) {
    Print (L"\r\n%s\r\n", Footer);
  }
}

VOID
AtUiDrawRow (
  IN BOOLEAN       Selected,
  IN CONST CHAR16 *Marker,
  IN CONST CHAR16 *Text
  )
{
  gST->ConOut->SetAttribute (gST->ConOut,
                             Selected ? AT_ATTR_SELECTED : AT_ATTR_NORMAL);
  Print (L"%s %s %s", Selected ? L">" : L" ",
         (Marker != NULL) ? Marker : L" ", (Text != NULL) ? Text : L"");
  gST->ConOut->SetAttribute (gST->ConOut, AT_ATTR_NORMAL);
  Print (L"\r\n");
}

UINTN
AtUiWindowStart (
  IN UINTN Cursor,
  IN UINTN Count,
  IN UINTN Rows
  )
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
AtUiMoveCursor (
  IN OUT UINTN *Cursor,
  IN UINTN     Count,
  IN AT_KEY    Key
  )
{
  if (Count == 0) {
    *Cursor = 0;
    return;
  }
  if (Key == AtKeyUp) {
    *Cursor = (*Cursor == 0) ? Count - 1 : *Cursor - 1;
  } else if (Key == AtKeyDown) {
    *Cursor = (*Cursor + 1 >= Count) ? 0 : *Cursor + 1;
  }
}

VOID
AtUiShowMessage (
  IN CONST CHAR16 *Text
  )
{
  gST->ConOut->SetAttribute (gST->ConOut, AT_ATTR_TITLE);
  gST->ConOut->ClearScreen (gST->ConOut);
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  Print (L"\r\n\r\n  %s\r\n", (Text != NULL) ? Text : L"");
  gST->ConOut->SetAttribute (gST->ConOut, AT_ATTR_NORMAL);
}

VOID
AtUiReportStatus (
  IN CONST CHAR16 *What,
  IN EFI_STATUS    Status
  )
{
  gST->ConOut->SetAttribute (gST->ConOut, AT_ATTR_NORMAL);
  Print (L"\r\n%s: %r\r\n", (What != NULL) ? What : L"", Status);
  Print (L"Press power to continue.\r\n");
  AtUiWaitForKey (0);
}

EFI_STATUS
AtUiRunMenu (
  IN  CONST CHAR16  *Title,
  IN  CONST CHAR16  **Items,
  IN  UINTN          Count,
  OUT UINTN         *Selected,
  IN  CONST CHAR16  *Footer
  )
{
  UINTN   Cursor = 0;
  UINTN   Start;
  UINTN   Index;
  UINTN   Visible;
  AT_KEY  Key;

  if (Items == NULL || Selected == NULL || Count == 0) {
    return EFI_INVALID_PARAMETER;
  }

  /* Drop anything held since launch so it does not move the cursor at once. */
  gST->ConIn->Reset (gST->ConIn, FALSE);

  Visible = (Count < AT_VISIBLE_ROWS) ? Count : AT_VISIBLE_ROWS;

  while (TRUE) {
    AtUiBeginScreen (Title, NULL);

    Start = AtUiWindowStart (Cursor, Count, Visible);
    for (Index = Start; Index < Start + Visible && Index < Count; Index++) {
      AtUiDrawRow ((BOOLEAN)(Index == Cursor), L" ", Items[Index]);
    }

    AtUiEndScreen (Footer);

    Key = AtUiWaitForKey (0);
    if (Key == AtKeySelect) {
      *Selected = Cursor;
      return EFI_SUCCESS;
    } else if (Key == AtKeyUp || Key == AtKeyDown) {
      AtUiMoveCursor (&Cursor, Count, Key);
    }
  }
}
