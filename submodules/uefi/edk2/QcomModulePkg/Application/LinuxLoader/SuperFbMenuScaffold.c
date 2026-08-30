/*
 * Shared data-driven menu runner for the super-fastboot UI.
 *
 * This translation unit intentionally contains no screen-specific policy. The
 * callbacks own discovery, drawing, actions and resource cleanup; the runner
 * owns the common lifecycle, scrolling and key dispatch. Keeping it separate
 * also lets the host launch tests compile the browser's menu paths without
 * pulling in the UEFI-only screen implementation.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbMenu.h"

#ifdef SFB_HOST_BUILD
/* test_launch.c supplies this shim before including the production sources. */
UINTN EFIAPI
Print (IN CONST CHAR16 *Format, ...);
#else
#include <Library/UefiLib.h>
#endif

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
