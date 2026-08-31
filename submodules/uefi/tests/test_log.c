/*
 * Host regression for the BDS log ring.
 *
 * The ring is deliberately tested through its public API only.  The expected
 * stream is kept independently so a bad circular-buffer rotation cannot hide
 * behind a length-only assertion.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* libc headers must precede every EDK2 header: ProcessorBind.h pushes hidden
 * symbol visibility and never pops it. */
#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#undef NULL

#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/ReportStatusCodeLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/ReportStatusCodeHandler.h>

#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLog.h"

#define RING_SIZE       (64U * 1024U)
#define HISTORY_CAPACITY (RING_SIZE * 4U)

EFI_BOOT_SERVICES *gBS;
EFI_GUID gEfiEventExitBootServicesGuid = EFI_EVENT_GROUP_EXIT_BOOT_SERVICES;
EFI_GUID gEfiRscHandlerProtocolGuid = EFI_RSC_HANDLER_PROTOCOL_GUID;

/* ---- the small EDK2 surface used by SuperFbLog.c ------------------------- */

VOID *EFIAPI
CopyMem (OUT VOID *Destination, IN CONST VOID *Source, IN UINTN Length)
{
  return memmove (Destination, Source, Length);
}

static UINTN
AppendChar (OUT CHAR8 *Buffer, IN UINTN BufferSize, IN UINTN Length, IN CHAR8 Value)
{
  if (BufferSize != 0 && Length + 1 < BufferSize) {
    Buffer[Length] = Value;
  }
  return Length + 1;
}

static UINTN
AppendString (
  OUT CHAR8        *Buffer,
  IN  UINTN         BufferSize,
  IN  UINTN         Length,
  IN  CONST CHAR8  *Value
  )
{
  while (*Value != '\0') {
    Length = AppendChar (Buffer, BufferSize, Length, *Value++);
  }
  return Length;
}

static UINTN
AppendUnsigned (
  OUT CHAR8 *Buffer,
  IN  UINTN  BufferSize,
  IN  UINTN  Length,
  IN  UINT64 Value
  )
{
  CHAR8 Digits[32];
  UINTN DigitCount;

  DigitCount = 0;
  do {
    Digits[DigitCount++] = (CHAR8)('0' + (Value % 10));
    Value /= 10;
  } while (Value != 0);
  while (DigitCount != 0) {
    Length = AppendChar (Buffer, BufferSize, Length, Digits[--DigitCount]);
  }
  return Length;
}

/* The test supplies only %s to SfbLogNote and the real implementation uses
 * %Lu for its drop notice.  Keeping this formatter small avoids pulling the
 * firmware PrintLib into the host binary while retaining those semantics. */
static UINTN
FormatVaList (
  OUT CHAR8        *Buffer,
  IN  UINTN         BufferSize,
  IN  CONST CHAR8  *Format,
  IN  VA_LIST       Marker
  )
{
  UINTN Length;

  Length = 0;
  while (*Format != '\0') {
    if (*Format != '%') {
      Length = AppendChar (Buffer, BufferSize, Length, *Format++);
      continue;
    }
    Format++;
    if (*Format == '%') {
      Length = AppendChar (Buffer, BufferSize, Length, *Format++);
    } else if (*Format == 's') {
      Length = AppendString (Buffer, BufferSize, Length,
                             VA_ARG (Marker, CONST CHAR8 *));
      Format++;
    } else if (*Format == 'L' && Format[1] == 'u') {
      Length = AppendUnsigned (Buffer, BufferSize, Length,
                               VA_ARG (Marker, UINT64));
      Format += 2;
    } else {
      /* No other format is reachable in this test's successful paths. */
      assert (FALSE);
    }
  }
  if (BufferSize != 0) {
    Buffer[(Length < BufferSize) ? Length : BufferSize - 1] = '\0';
  }
  return Length;
}

UINTN EFIAPI
AsciiVSPrint (
  OUT CHAR8        *StartOfBuffer,
  IN  UINTN         BufferSize,
  IN  CONST CHAR8  *FormatString,
  IN  VA_LIST       Marker
  )
{
  return FormatVaList (StartOfBuffer, BufferSize, FormatString, Marker);
}

UINTN EFIAPI
AsciiSPrint (
  OUT CHAR8        *StartOfBuffer,
  IN  UINTN         BufferSize,
  IN  CONST CHAR8  *FormatString,
  ...
  )
{
  VA_LIST Marker;
  UINTN   Length;

  VA_START (Marker, FormatString);
  Length = FormatVaList (StartOfBuffer, BufferSize, FormatString, Marker);
  VA_END (Marker);
  return Length;
}

UINTN EFIAPI
AsciiBSPrint (
  OUT CHAR8        *StartOfBuffer,
  IN  UINTN         BufferSize,
  IN  CONST CHAR8  *FormatString,
  IN  BASE_LIST     Marker
  )
{
  /* The status-code callback is not invoked by these ring tests. */
  (void)FormatString;
  (void)Marker;
  if (BufferSize != 0) {
    StartOfBuffer[0] = '\0';
  }
  return 0;
}

BOOLEAN EFIAPI
ReportStatusCodeExtractDebugInfo (
  IN CONST EFI_STATUS_CODE_DATA  *Data,
  OUT UINT32                     *ErrorLevel,
  OUT BASE_LIST                  *Marker,
  OUT CHAR8                     **Format
  )
{
  (void)Data;
  (void)ErrorLevel;
  (void)Marker;
  (void)Format;
  return FALSE;
}

/* ---- fake status-code handler and the three Boot Services touched -------- */

static EFI_RSC_HANDLER_PROTOCOL mRscHandler;
static EFI_RSC_HANDLER_CALLBACK mRegisteredCallback;
static EFI_EVENT                mEvent = (EFI_EVENT)(UINTN)0xE71;
static UINTN                    mLocateCount;
static UINTN                    mCreateCount;
static UINTN                    mCloseCount;
static UINTN                    mRegisterCount;
static UINTN                    mUnregisterCount;

static EFI_STATUS EFIAPI
FakeRegister (IN EFI_RSC_HANDLER_CALLBACK Callback, IN EFI_TPL Tpl)
{
  assert (Callback != NULL);
  assert (Tpl == TPL_HIGH_LEVEL);
  assert (mRegisteredCallback == NULL);
  mRegisteredCallback = Callback;
  mRegisterCount++;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeUnregister (IN EFI_RSC_HANDLER_CALLBACK Callback)
{
  assert (Callback == mRegisteredCallback);
  mRegisteredCallback = NULL;
  mUnregisterCount++;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeLocateProtocol (
  IN  EFI_GUID  *Protocol,
  IN  VOID      *Registration,
  OUT VOID     **Interface
  )
{
  (void)Registration;
  assert (Protocol != NULL);
  assert (Interface != NULL);
  mLocateCount++;
  if (memcmp (Protocol, &gEfiRscHandlerProtocolGuid, sizeof (EFI_GUID)) != 0) {
    *Interface = NULL;
    return EFI_NOT_FOUND;
  }
  *Interface = &mRscHandler;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeCreateEventEx (
  IN       UINT32                 Type,
  IN       EFI_TPL                NotifyTpl,
  IN       EFI_EVENT_NOTIFY       NotifyFunction,
  IN CONST VOID                   *NotifyContext,
  IN CONST EFI_GUID               *EventGroup,
  OUT      EFI_EVENT              *Event
  )
{
  static const EFI_GUID ExitGroup = EFI_EVENT_GROUP_EXIT_BOOT_SERVICES;

  (void)NotifyContext;
  assert (Type == EVT_NOTIFY_SIGNAL);
  assert (NotifyTpl == TPL_NOTIFY);
  assert (NotifyFunction != NULL);
  assert (EventGroup != NULL);
  assert (memcmp (EventGroup, &ExitGroup, sizeof (ExitGroup)) == 0);
  assert (Event != NULL);
  *Event = mEvent;
  mCreateCount++;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeCloseEvent (IN EFI_EVENT Event)
{
  assert (Event == mEvent);
  mCloseCount++;
  return EFI_SUCCESS;
}

static void
ResetBootServices (void)
{
  static EFI_BOOT_SERVICES BootServices;

  memset (&BootServices, 0, sizeof (BootServices));
  BootServices.LocateProtocol = FakeLocateProtocol;
  BootServices.CreateEventEx = FakeCreateEventEx;
  BootServices.CloseEvent = FakeCloseEvent;
  gBS = &BootServices;

  mRscHandler.Register = FakeRegister;
  mRscHandler.Unregister = FakeUnregister;
  mRegisteredCallback = NULL;
  mLocateCount = 0;
  mCreateCount = 0;
  mCloseCount = 0;
  mRegisterCount = 0;
  mUnregisterCount = 0;
}

/* ---- independent stream model ------------------------------------------- */

static CHAR8 mHistory[HISTORY_CAPACITY];
static UINTN mHistoryLength;
static UINT32 mNextSequence;

static void
AppendExpected (IN CONST CHAR8 *Line)
{
  UINTN Length;

  Length = strlen (Line);
  assert (Length != 0);
  assert (Line[Length - 1] == '\n');
  assert (Length <= 1022);
  assert (mHistoryLength + Length <= sizeof (mHistory));
  SfbLogNote ("%s", Line);
  memcpy (&mHistory[mHistoryLength], Line, Length);
  mHistoryLength += Length;
}

static void
AppendGenerated (IN UINTN Length)
{
  CHAR8 Line[1024];
  int   PrefixLength;
  UINTN Index;

  assert (Length >= 12 && Length <= sizeof (Line) - 1);
  PrefixLength = snprintf (Line, sizeof (Line), "seq-%05u:", mNextSequence++);
  assert (PrefixLength > 0);
  assert ((UINTN)PrefixLength + 1 <= Length);
  for (Index = (UINTN)PrefixLength; Index + 1 < Length; Index++) {
    Line[Index] = (CHAR8)('A' + ((mNextSequence + Index) % 26));
  }
  Line[Length - 1] = '\n';
  Line[Length] = '\0';
  AppendExpected (Line);
}

static UINT64
ReadDropCount (IN CONST CHAR8 *Snapshot, OUT CONST CHAR8 **Payload)
{
  static const CHAR8 Prefix[] = "[canoe] dropped ";
  CONST CHAR8         *Cursor;
  UINT64               Count;

  assert (strncmp (Snapshot, Prefix, sizeof (Prefix) - 1) == 0);
  Cursor = Snapshot + sizeof (Prefix) - 1;
  assert (*Cursor >= '0' && *Cursor <= '9');
  Count = 0;
  while (*Cursor >= '0' && *Cursor <= '9') {
    Count = Count * 10 + (UINT64)(*Cursor - '0');
    Cursor++;
  }
  assert (strncmp (Cursor, " bytes before this point\n", 25) == 0);
  *Payload = Cursor + 25;
  return Count;
}

static void
AssertSnapshotMatchesModel (void)
{
  CHAR8       *Snapshot;
  CONST CHAR8 *Payload;
  UINTN        Length;
  UINTN        PayloadLength;
  UINTN        NoticeLength;
  UINT64       ExpectedDropped;

  Length = 0;
  Snapshot = SfbLogSnapshot (&Length);
  assert (Snapshot != NULL);
  assert (Snapshot[Length] == '\0');
  /* Every borrow is released, including on the early return: an unreleased
     snapshot refuses later appends, so a missing release here would show up
     as a wrong payload several cases later rather than as a failure at the
     call that caused it. */
  if (mHistoryLength <= RING_SIZE) {
    assert (Length == mHistoryLength);
    assert (memcmp (Snapshot, mHistory, mHistoryLength) == 0);
    SfbLogRelease ();
    return;
  }

  ExpectedDropped = (UINT64)(mHistoryLength - RING_SIZE);
  assert (ReadDropCount (Snapshot, &Payload) == ExpectedDropped);
  NoticeLength = (UINTN)(Payload - Snapshot);
  PayloadLength = RING_SIZE;
  assert (Length == NoticeLength + PayloadLength);
  assert (memcmp (Payload, &mHistory[mHistoryLength - PayloadLength],
                  PayloadLength) == 0);
  SfbLogRelease ();
}

/* ---- cases --------------------------------------------------------------- */

static void
TestEmptyRing (void)
{
  CHAR8 *Snapshot;
  UINTN  Length;

  Length = 1234;
  Snapshot = SfbLogSnapshot (&Length);
  assert (Snapshot == NULL);
  assert (Length == 0);
  /* Nothing was borrowed, so nothing is released - releasing here could free a
     borrow this test does not own. */
}

static void
TestRoundTripAndAppendAfterSnapshot (void)
{
  AppendExpected ("simple: first\n");
  AppendExpected ("simple: second\n");
  AppendExpected ("simple: third\n");
  AssertSnapshotMatchesModel ();
  /* No overflow yet: the model comparison also proves there is no drop line. */
  AssertSnapshotMatchesModel ();
  AppendExpected ("simple: after snapshot\n");
  AssertSnapshotMatchesModel ();
}

static void
TestOverflowMidLine (void)
{
  const UINTN TargetBeforeWrap = RING_SIZE - 9;

  while (mHistoryLength + 40 < RING_SIZE - 20) {
    AppendGenerated (37);
  }
  assert (mHistoryLength < TargetBeforeWrap);
  AppendGenerated (TargetBeforeWrap - mHistoryLength);
  assert (mHistoryLength == TargetBeforeWrap);
  /* Nine bytes remain in the physical ring; this line wraps halfway through. */
  AppendGenerated (25);
  assert (mHistoryLength == RING_SIZE + 16);
  AssertSnapshotMatchesModel ();
}

static void
TestOverflowAtLineBoundary (void)
{
  UINTN Index;

  /* SfbLogSnapshot deliberately resets the write offset.  These writes total
   * exactly one ring after that reset, with the final line ending at offset 0. */
  for (Index = 0; Index < 65; Index++) {
    AppendGenerated (1000);
  }
  AppendGenerated (536);
  assert (mHistoryLength == (RING_SIZE * 2) + 16);
  AssertSnapshotMatchesModel ();
}

/* The borrow contract, which nothing else here exercises: while a snapshot is
   held the ring must not move, because the caller is writing those exact bytes
   to a file. Appending directly rather than through the model is the point -
   this is what a status-code callback does mid-flush.

   Runs last: the refused bytes are counted as drops, which would perturb the
   drop arithmetic the overflow cases assert. */
static void
TestBorrowIsExclusive (void)
{
  CHAR8 *Snapshot;
  CHAR8 *Second;
  UINTN  Length;
  UINTN  Probe;
  UINTN  Index;
  CHAR8  Before[256];

  Length = 0;
  Snapshot = SfbLogSnapshot (&Length);
  assert (Snapshot != NULL);
  assert (Length >= sizeof (Before));
  for (Index = 0; Index < sizeof (Before); Index++) {
    Before[Index] = Snapshot[Index];
  }

  SfbLogAppend ("borrow: this must be refused\n", 29);
  for (Index = 0; Index < sizeof (Before); Index++) {
    assert (Snapshot[Index] == Before[Index]);
  }

  /* A second borrow is refused rather than rotating the ring out from under
     the first, and it reports no bytes instead of leaving the count alone. */
  Probe = 1234;
  Second = SfbLogSnapshot (&Probe);
  assert (Second == NULL);
  assert (Probe == 0);

  SfbLogRelease ();

  /* Released: capture resumes and a borrow is available again. */
  Probe = 0;
  Second = SfbLogSnapshot (&Probe);
  assert (Second != NULL);
  assert (Probe > 0);
  SfbLogRelease ();
}

static void
TestLifecycle (void)
{
  UINTN Locate;
  UINTN Create;
  UINTN Register;

  /* This is the no-prior-Begin path, including a null gBS like a failed setup. */
  gBS = NULL;
  SfbLogEnd ();

  ResetBootServices ();
  SfbLogEnd ();
  assert (mLocateCount == 0);
  assert (mCreateCount == 0);
  assert (mCloseCount == 0);
  assert (mRegisterCount == 0);
  assert (mUnregisterCount == 0);

  SfbLogBegin ();
  assert (mLocateCount == 1);
  assert (mCreateCount == 1);
  assert (mRegisterCount == 1);
  assert (mRegisteredCallback != NULL);
  Locate = mLocateCount;
  Create = mCreateCount;
  Register = mRegisterCount;
  SfbLogBegin ();
  assert (mLocateCount == Locate);
  assert (mCreateCount == Create);
  assert (mRegisterCount == Register);

  SfbLogEnd ();
  assert (mUnregisterCount == 1);
  assert (mCloseCount == 1);
  assert (mRegisteredCallback == NULL);
  SfbLogEnd ();
  assert (mUnregisterCount == 1);
  assert (mCloseCount == 1);
  assert (mRegisterCount == mUnregisterCount);
}

int
main (void)
{
  TestEmptyRing ();
  TestRoundTripAndAppendAfterSnapshot ();
  TestOverflowMidLine ();
  TestOverflowAtLineBoundary ();
  TestLifecycle ();
  TestBorrowIsExclusive ();
  printf ("test_log: all cases passed\n");
  return 0;
}
