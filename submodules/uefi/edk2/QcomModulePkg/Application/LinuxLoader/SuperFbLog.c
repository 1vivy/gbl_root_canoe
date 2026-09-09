/*
 * Tap the BDS status-code stream into the log ring.
 *
 * Every DEBUG() in this image already travels through EDK2's status-code
 * router to the platform's serial handler. Registering a second listener costs
 * no new logging API and no call-site changes: existing marks start landing in
 * a buffer we own, in addition to wherever they already went.
 *
 * The ring itself is SuperFbLogRing.c. This file owns only the registration
 * lifecycle and the decode of one status-code record.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "SuperFbLog.h"
#include <Guid/EventGroup.h>
#include <Library/PrintLib.h>
#include <Library/ReportStatusCodeLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/ReportStatusCodeHandler.h>

#define SFB_LOG_RENDER_SIZE     1024U

STATIC volatile BOOLEAN          mSfbLogDisabled;
STATIC BOOLEAN                   mSfbLogRegistered;
STATIC EFI_EVENT                 mSfbLogExitEvent;
STATIC EFI_RSC_HANDLER_PROTOCOL  *mSfbLogRscHandler;

STATIC EFI_STATUS
EFIAPI
SfbLogStatusCodeCallback (
  IN EFI_STATUS_CODE_TYPE CodeType, IN EFI_STATUS_CODE_VALUE Value,
  IN UINT32 Instance, IN EFI_GUID *CallerId, IN EFI_STATUS_CODE_DATA *Data
  )
{
  CHAR8     Buffer[SFB_LOG_RENDER_SIZE];
  UINT32    ErrorLevel;
  BASE_LIST Marker;
  CHAR8     *Format;
  UINTN     CharCount;
  /* EBS can reclaim this image; test the neuter flag before touching anything. */
  if (mSfbLogDisabled) {
    return EFI_SUCCESS;
  }
  (VOID)CallerId;
  Buffer[0] = '\0';
  if (Data != NULL &&
      ReportStatusCodeExtractDebugInfo (Data, &ErrorLevel, &Marker, &Format)) {
    (VOID)ErrorLevel;
    CharCount = AsciiBSPrint (Buffer, sizeof (Buffer), Format, Marker);
  } else if ((CodeType & EFI_STATUS_CODE_TYPE_MASK) == EFI_ERROR_CODE) {
    CharCount = AsciiSPrint (Buffer, sizeof (Buffer),
                             "ERROR: C%08x:V%08x I%x\n", CodeType, Value, Instance);
  } else if ((CodeType & EFI_STATUS_CODE_TYPE_MASK) == EFI_PROGRESS_CODE) {
    CharCount = AsciiSPrint (Buffer, sizeof (Buffer),
                             "PROGRESS: V%08x I%x\n", Value, Instance);
  } else {
    /* Opaque records are skipped: arbitrary payload decoding is not bounded at HIGH. */
    return EFI_SUCCESS;
  }
  /* Clamp to leave room for the terminator, so a record that renders to
     exactly the buffer length still ends a line. Without the reserve the
     newline was skipped at that one length and the record ran into the next
     one in the ring. */
  if (CharCount >= sizeof (Buffer) - 1) {
    CharCount = sizeof (Buffer) - 2;
  }
  if (CharCount == 0 || Buffer[CharCount - 1] != '\n') {
    Buffer[CharCount++] = '\n';
  }
  SfbLogAppend (Buffer, CharCount);
  return EFI_SUCCESS;
}

STATIC VOID
EFIAPI
SfbLogExitBootServices (IN EFI_EVENT Event, IN VOID *Context)
{
  (VOID)Event;
  (VOID)Context;
  mSfbLogDisabled = TRUE;
}

VOID SfbLogBegin (VOID)
{
  EFI_STATUS               Status;
  EFI_RSC_HANDLER_PROTOCOL *RscHandler;
  if (mSfbLogRegistered) {
    return;
  }
  mSfbLogDisabled = FALSE;
  RscHandler = NULL;
  if (gBS == NULL) {
    SfbLogNote ("SFB: log-capture unavailable: no boot services");
    return;
  }
  Status = gBS->LocateProtocol (&gEfiRscHandlerProtocolGuid, NULL,
                                (VOID **)&RscHandler);
  if (EFI_ERROR (Status) || RscHandler == NULL || RscHandler->Register == NULL) {
    if (!EFI_ERROR (Status)) {
      Status = EFI_UNSUPPORTED;
    }
    SfbLogNote ("SFB: log-capture register unavailable: %r", Status);
    return;
  }
  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY,
                               SfbLogExitBootServices, NULL,
                               &gEfiEventExitBootServicesGuid, &mSfbLogExitEvent);
  if (EFI_ERROR (Status)) {
    SfbLogNote ("SFB: log-capture exit event failed: %r", Status);
    return;
  }
  Status = RscHandler->Register (SfbLogStatusCodeCallback, TPL_HIGH_LEVEL);
  if (EFI_ERROR (Status)) {
    mSfbLogDisabled = TRUE;
    SfbLogNote ("SFB: log-capture register failed: %r", Status);
    Status = gBS->CloseEvent (mSfbLogExitEvent);
    if (!EFI_ERROR (Status)) {
      mSfbLogExitEvent = NULL;
    }
    return;
  }
  mSfbLogRscHandler = RscHandler;
  mSfbLogRegistered = TRUE;
  SfbLogNote ("SFB: log-capture registered");
}

VOID SfbLogEnd (VOID)
{
  EFI_STATUS Status;
  mSfbLogDisabled = TRUE;
  if (!mSfbLogRegistered && mSfbLogExitEvent == NULL) {
    return;
  }
  if (mSfbLogRegistered && mSfbLogRscHandler != NULL &&
      mSfbLogRscHandler->Unregister != NULL) {
    Status = mSfbLogRscHandler->Unregister (SfbLogStatusCodeCallback);
    if (EFI_ERROR (Status)) {
      SfbLogNote ("SFB: log-capture unregister failed: %r", Status);
    } else {
      mSfbLogRegistered = FALSE;
      mSfbLogRscHandler = NULL;
    }
  } else if (mSfbLogRegistered) {
    SfbLogNote ("SFB: log-capture unregister unavailable");
  }
  /* Begin refuses to run without Boot Services, so End must not assume they
     are still there either - this is the path taken when things are already
     going wrong. */
  if (mSfbLogExitEvent != NULL && gBS != NULL) {
    Status = gBS->CloseEvent (mSfbLogExitEvent);
    if (EFI_ERROR (Status)) {
      SfbLogNote ("SFB: log-capture exit event close failed: %r", Status);
    } else {
      mSfbLogExitEvent = NULL;
    }
  }
  if (!mSfbLogRegistered && mSfbLogExitEvent == NULL) {
    SfbLogNote ("SFB: log-capture ended");
  }
}
