/*
 * The ring the Canoe BDS log lives in, and the ordering it is read back in.
 *
 * Knows nothing about where lines come from or where they go: a producer
 * appends bytes, a consumer asks for them oldest-first. The capture side and
 * the flush side each change for their own reasons, and neither reason is a
 * reason to touch this file.
 *
 * Storage is one static array and the ring itself never allocates. That is
 * deliberate: this log exists to describe boots that went wrong, and a buffer
 * that needs a healthy heap to hold a message fails exactly when it is most
 * wanted. The flush path is not allocation-free - enumerating handles goes
 * through LocateHandleBuffer - but nothing on it needs memory to preserve what
 * was already captured.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "SuperFbLog.h"
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>

#define SFB_LOG_RING_SIZE       (64U * 1024U)
#define SFB_LOG_NOTICE_SIZE     96U
#define SFB_LOG_STORAGE_SIZE    (SFB_LOG_RING_SIZE + SFB_LOG_NOTICE_SIZE)
#define SFB_LOG_NOTE_SIZE       1024U

STATIC CHAR8  mSfbLogRing[SFB_LOG_STORAGE_SIZE];
STATIC UINTN  mSfbLogDataBase;
STATIC UINTN  mSfbLogWriteOffset;
STATIC UINTN  mSfbLogLength;
STATIC volatile UINT64 mSfbLogDroppedBytes;

/* HIGH-level callbacks can interrupt a snapshot or note. The busy flag makes
   them mutually exclusive; an interrupting callback drops its whole line and
   the byte count is reported in the snapshot, so a gap is never silent.

   The claim is a plain test-then-set, which leaves a window of a couple of
   instructions where an interrupting callback also reads it clear and the two
   sides interleave characters within one line. Closing that window needs
   either an interlocked primitive - SynchronizationLib is not mapped for this
   package - or RaiseTPL/RestoreTPL inside a path the callback also runs, and
   neither is worth a package-wide dependency or Boot Services calls at
   TPL_HIGH_LEVEL for a garbled line. The indices stay in range regardless:
   mSfbLogDataBase is at most the notice reserve and the offset is taken modulo
   the ring, so the worst case lands on the last storage byte.

   A borrow extends that claim: SfbLogSnapshot keeps it held until
   SfbLogRelease, so the ring cannot change while a caller is writing bytes
   that live in it. Appends during a borrow are counted, and reported only if
   a later snapshot happens - with one flush per boot there is none, so the
   file-system driver's own chatter during our write is discarded. That is the
   accepted cost: those bytes had no reader anyway, and the alternative was
   tracking room before a wrap, which has to be right about the full-ring case
   and was not. */
STATIC volatile BOOLEAN mSfbLogAccessBusy;

VOID
SfbLogAppend (IN CONST CHAR8 *Text, IN UINTN Length)
{
  UINTN Index;
  if (Text == NULL || Length == 0) { return; }
  if (mSfbLogAccessBusy) {
    mSfbLogDroppedBytes += (UINT64)Length;
    return;
  }
  mSfbLogAccessBusy = TRUE;
  for (Index = 0; Index < Length; Index++) {
    mSfbLogRing[mSfbLogDataBase + mSfbLogWriteOffset] = Text[Index];
    mSfbLogWriteOffset = (mSfbLogWriteOffset + 1) % SFB_LOG_RING_SIZE;
    if (mSfbLogLength < SFB_LOG_RING_SIZE) { mSfbLogLength++; }
    else { mSfbLogDroppedBytes++; }
  }
  mSfbLogAccessBusy = FALSE;
}
STATIC VOID
SfbLogReverse (IN CHAR8 *Buffer, IN UINTN First, IN UINTN Last)
{
  CHAR8 Temp;
  while (First < Last) {
    Temp = Buffer[First]; Buffer[First] = Buffer[Last]; Buffer[Last] = Temp;
    First++; Last--;
  }
}

/* Rotate the circular area so its logical zero is physically first. Three
   reversals in place rather than a copy into a second buffer: the second
   buffer would double this file's footprint in a raw partition image, and the
   ring is read at most twice per boot. */
STATIC VOID
SfbLogRotate (IN UINTN Offset)
{
  CHAR8 *Buffer;
  if (Offset == 0) { return; }
  Buffer = &mSfbLogRing[mSfbLogDataBase];
  SfbLogReverse (Buffer, 0, Offset - 1);
  SfbLogReverse (Buffer, Offset, SFB_LOG_RING_SIZE - 1);
  SfbLogReverse (Buffer, 0, SFB_LOG_RING_SIZE - 1);
}

CHAR8 *
SfbLogSnapshot (OUT UINTN *Length)
{
  CHAR8  Notice[SFB_LOG_NOTICE_SIZE];
  UINTN  Count, Oldest, Prefix, Total;
  UINT64 Dropped;
  if (Length == NULL) { return NULL; }
  *Length = 0;
  /* Refused while a borrow is outstanding, so the ring cannot be rotated out
     from under a pointer already handed out. Length is zeroed first: NULL
     always means the caller got no bytes, whatever the reason. */
  if (mSfbLogAccessBusy) { return NULL; }
  mSfbLogAccessBusy = TRUE;
  Count = mSfbLogLength; Dropped = mSfbLogDroppedBytes;
  if (Count == 0 && Dropped == 0) {
    mSfbLogAccessBusy = FALSE; return NULL;
  }
  Prefix = 0;
  if (Dropped != 0) {
    Prefix = AsciiSPrint (Notice, sizeof (Notice),
                          "[canoe] dropped %Lu bytes before this point\n", Dropped);
    if (Prefix >= sizeof (Notice)) { Prefix = sizeof (Notice) - 1; }
  }
  Oldest = (mSfbLogWriteOffset + SFB_LOG_RING_SIZE - Count) % SFB_LOG_RING_SIZE;
  SfbLogRotate (Oldest);
  /* Overlapping move: EDK2 CopyMem is defined for overlap. */
  CopyMem (mSfbLogRing + Prefix, mSfbLogRing + mSfbLogDataBase, Count);
  if (Prefix != 0) { CopyMem (mSfbLogRing, Notice, Prefix); }
  mSfbLogDataBase = Prefix; mSfbLogWriteOffset = Count % SFB_LOG_RING_SIZE;
  Total = Prefix + Count; mSfbLogRing[Total] = '\0'; *Length = Total;
  /* The busy claim is deliberately NOT dropped here: it is what makes the
     borrow exclusive. It also means a second snapshot before the release is
     refused at the guard above rather than rotating the ring out from under
     the pointer already handed out. */
  return mSfbLogRing;
}

VOID
SfbLogRelease (VOID)
{
  mSfbLogAccessBusy = FALSE;
}

VOID
SfbLogNote (IN CONST CHAR8 *Format, ...)
{
  CHAR8 Buffer[SFB_LOG_NOTE_SIZE]; UINTN CharCount; VA_LIST Marker;
  if (Format == NULL) { return; }
  VA_START (Marker, Format);
  CharCount = AsciiVSPrint (Buffer, sizeof (Buffer) - 1, Format, Marker);
  VA_END (Marker);
  if (CharCount > sizeof (Buffer) - 2) { CharCount = sizeof (Buffer) - 2; }
  if (CharCount == 0 || Buffer[CharCount - 1] != '\n') { Buffer[CharCount++] = '\n'; }
  SfbLogAppend (Buffer, CharCount);
}
