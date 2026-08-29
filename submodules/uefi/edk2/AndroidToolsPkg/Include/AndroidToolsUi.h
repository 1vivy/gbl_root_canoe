/** @file
 *  Console menu UI for AndroidToolsPkg, modeled on the super-fastboot boot
 *  menu (SuperFbMenu). Three keys drive everything: volume up and volume down
 *  move the cursor, and power confirms.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __ANDROID_TOOLS_UI_H__
#define __ANDROID_TOOLS_UI_H__

#include <Uefi.h>
#include <Library/PrintLib.h>

typedef enum {
  AtKeyTimeout = 0,
  AtKeyUp,
  AtKeyDown,
  AtKeySelect
} AT_KEY;

/* Rows of list content a screen shows before it starts scrolling. */
#define AT_VISIBLE_ROWS  12

/**
  Announce the menu and wait for the launching key to be released, then drain
  the input queue. Call once at application entry, before the first AtUiRunMenu,
  so a power press held through LoadImage/StartImage does not auto-confirm the
  first entry.
**/
VOID
AtUiEnterMenu (
  IN CONST CHAR16 *Title
  );

/**
  Wait for one logical key action. TimeoutMs of 0 waits indefinitely. Returns
  AtKeyTimeout when the timer elapses. Power/select waits briefly and drains
  duplicate carriage returns from the same physical press; volume keys remain
  immediate.
**/
AT_KEY
AtUiWaitForKey (
  IN UINT32 TimeoutMs
  );

/** Drop both raw console events and a retained transition-time volume key. **/
VOID
AtUiResetInput (VOID);

/** Clear the screen and print a title (and optional subtitle). **/
VOID
AtUiBeginScreen (
  IN CONST CHAR16 *Title,
  IN CONST CHAR16 *Subtitle OPTIONAL
  );

/** Print a footer line. **/
VOID
AtUiEndScreen (
  IN CONST CHAR16 *Footer
  );

/** Print one menu row, highlighted when Selected. **/
VOID
AtUiDrawRow (
  IN BOOLEAN       Selected,
  IN CONST CHAR16 *Marker,
  IN CONST CHAR16 *Text
  );

/** First row of the visible window, chosen to keep Cursor inside it. **/
UINTN
AtUiWindowStart (
  IN UINTN Cursor,
  IN UINTN Count,
  IN UINTN Rows
  );

/** Move a cursor by one step, wrapping at the ends. **/
VOID
AtUiMoveCursor (
  IN OUT UINTN *Cursor,
  IN UINTN     Count,
  IN AT_KEY    Key
  );

/**
  Clear the screen and show a single centered message (e.g. "Restarting...").
**/
VOID
AtUiShowMessage (
  IN CONST CHAR16 *Text
  );

/**
  Report a failure and hold the screen until the user acknowledges it.
**/
VOID
AtUiReportStatus (
  IN CONST CHAR16 *What,
  IN EFI_STATUS    Status
  );

/**
  Run a menu of Count text items under Title. Volume up/down move the cursor,
  power selects. Returns EFI_SUCCESS and writes the chosen index to *Selected,
  or EFI_TIMEOUT. Footer is printed below the list.
**/
EFI_STATUS
AtUiRunMenu (
  IN  CONST CHAR16  *Title,
  IN  CONST CHAR16  **Items,
  IN  UINTN          Count,
  OUT UINTN         *Selected,
  IN  CONST CHAR16  *Footer OPTIONAL
  );

/*
 * Bounded text report: a fixed-capacity array of rows a collector fills and
 * the UI pages or a dump writes. Shared by every tool in this package; a
 * report that overflows its capacity truncates and says so, rather than
 * overrunning.
 */
#define AT_ROW_CHARS  96u

typedef struct {
  CHAR16 Text[AT_ROW_CHARS];
} AT_ROW;

typedef struct {
  AT_ROW  *Rows;
  UINTN   Count;
  UINTN   Capacity;
  BOOLEAN Truncated;
} AT_REPORT;

typedef EFI_STATUS (*AT_REPORT_BUILDER)(OUT AT_REPORT *Report);

typedef struct {
  CONST CHAR16     *Title;
  AT_REPORT_BUILDER Builder;
} AT_REPORT_SOURCE;

EFI_STATUS
AtReportInit (
  OUT AT_REPORT *Report,
  IN  UINTN     Capacity
  );

VOID
AtReportFree (
  IN OUT AT_REPORT *Report
  );

/* Next writable row, or NULL (setting Truncated) when the report is full. */
CHAR16 *
AtReportNextRow (
  IN OUT AT_REPORT *Report
  );

#define AtReportAdd(Report, Format, ...) do {                              \
  AT_REPORT *AtReport__ = (Report);                                        \
  CHAR16 *AtReportRow__ = AtReportNextRow (AtReport__);                    \
  if (AtReportRow__ != NULL) {                                             \
    UINTN AtReportLength__ = UnicodeSPrint (                               \
      AtReportRow__, AT_ROW_CHARS * sizeof (CHAR16),                       \
      (Format), ##__VA_ARGS__);                                            \
    if (AtReportLength__ >= AT_ROW_CHARS - 1) {                            \
      AtReport__->Truncated = TRUE;                                        \
    }                                                                      \
  }                                                                        \
} while (FALSE)

/**
  Build Source's report, page it on the console (volume keys page, power
  returns), and free it. Reports the build status instead when it fails.
**/
VOID
AtUiShowReport (
  IN CONST AT_REPORT_SOURCE *Source
  );

#endif /* __ANDROID_TOOLS_UI_H__ */
