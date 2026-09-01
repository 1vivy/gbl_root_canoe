/*
 * Capturing the BDS status-code stream before it can be lost at handoff.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SUPER_FB_LOG_H__
#define __SUPER_FB_LOG_H__

#include <Uefi.h>
#include <Protocol/SimpleFileSystem.h>
#include "SuperFbLastLaunch.h"

/* Start capturing this image's log output into a ring the BDS owns. Idempotent. */
VOID    SfbLogBegin (VOID);

/* Stop capturing and release the handler registration. Idempotent; MUST be safe
   to call on every exit path, including after a failed SfbLogBegin. */
VOID    SfbLogEnd (VOID);
/* Borrow the captured text, oldest first, NUL-terminated, as one linear block.
   *Length receives the byte count excluding the terminator. Returns NULL, with
   *Length zeroed, when nothing was captured or when another borrow is still
   outstanding - so NULL means "no bytes for you", not specifically "empty".

   The block points into the ring, and capture continues while the caller holds
   it - a status-code callback can still fire. The borrow is therefore exclusive
   until SfbLogRelease: until then, appends that would reach these bytes are
   refused and counted rather than overwriting them. Every path that takes a
   snapshot MUST release it, including its failure paths. */
CHAR8 * SfbLogSnapshot (OUT UINTN *Length);

/* Give back a borrowed snapshot and let capture resume. Call this only when
   SfbLogSnapshot returned non-NULL: a NULL return can mean someone else holds
   the borrow, and releasing it then would resume the producer underneath them. */
VOID    SfbLogRelease (VOID);

/* Append pre-formatted bytes. The capture path uses this rather than
   SfbLogNote because a status-code record is already rendered, and re-running
   it through a format string would interpret any % the producer emitted. */
VOID    SfbLogAppend (IN CONST CHAR8 *Text, IN UINTN Length);

/* Append one formatted line directly, bypassing the status-code path. */
VOID    SfbLogNote (IN CONST CHAR8 *Format, ...);

/* Write the captured log to logfs. Tag is a short ASCII reason recorded in the
   session header (e.g. "stage4", "pre-launch"). Returns EFI_SUCCESS only when a
   file was written and closed. Fail-soft: never fatal to the caller. */
EFI_STATUS SfbLogFlush (IN CONST CHAR8 *Tag);

/* Persist/read the canonical last managed-launch value in logfs\\canoe.
 * A failed write deletes the partial replacement, and readers validate the
 * complete grammar before publishing it. */
EFI_STATUS SfbLastLaunchRead (OUT CHAR8 *Value, IN UINTN ValueBytes);

/* Open this session's log file in the rotation, evicting the oldest slot when
   all are taken. Declared here because the writer and the rotation live in
   separate files: a hand-copied prototype breaks at link, not at compile. */
EFI_STATUS SfbLogOpenSlot (
  IN  EFI_FILE_PROTOCOL  *Directory,
  OUT EFI_FILE_PROTOCOL **File
  );

#endif /* __SUPER_FB_LOG_H__ */
