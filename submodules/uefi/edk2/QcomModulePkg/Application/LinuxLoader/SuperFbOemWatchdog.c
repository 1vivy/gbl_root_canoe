/*
 * Cancel the OEM boot-failure applet's reset timer, where one exists.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbOemWatchdog.h"

#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

/*
 * Published by the applet that owns the timer. Present only on firmware that
 * ships it; stock Qualcomm firmware does not, and neither do other vendors, so
 * LocateProtocol returning EFI_NOT_FOUND is the ordinary case rather than an
 * error.
 */
STATIC EFI_GUID mSfbOemWatchdogProtocolGuid = {
  0x7D2A39F3, 0x0F8C, 0x47A0,
  { 0x9B, 0x51, 0xF2, 0x69, 0xB4, 0xBA, 0xF9, 0x93 }
};

/* One-shot: a second StopWatchdog would close an already freed event. */
STATIC BOOLEAN mSfbOemWatchdogAttempted = FALSE;

VOID
SfbOemWatchdogDisable (VOID)
{
  SFB_OEM_WDOG_PROTOCOL *Wdog = NULL;
  EFI_STATUS             Status;

  if (mSfbOemWatchdogAttempted) {
    return;
  }
  mSfbOemWatchdogAttempted = TRUE;

  Status = gBS->LocateProtocol (&mSfbOemWatchdogProtocolGuid, NULL,
                                (VOID **)&Wdog);
  if (EFI_ERROR (Status) || Wdog == NULL) {
    /* No applet on this device: there is nothing to disable. */
    DEBUG ((EFI_D_ERROR, "SFB: MARK oem-wdog state=absent status=%r\n",
            Status));
    return;
  }

  if (Wdog->Revision != SFB_OEM_WDOG_REVISION_DECODED) {
    /* Same GUID, layout we have not decoded: calling through it would be a
     * guess about where StopWatchdog sits. Leave the timer armed instead. */
    DEBUG ((EFI_D_ERROR, "SFB: MARK oem-wdog state=unknown-rev rev=0x%llx\n",
            (UINT64)Wdog->Revision));
    return;
  }

  if (Wdog->StopWatchdog == NULL) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK oem-wdog state=no-stop rev=0x%llx\n",
            (UINT64)Wdog->Revision));
    return;
  }

  Status = Wdog->StopWatchdog ();
  DEBUG ((EFI_D_ERROR, "SFB: MARK oem-wdog state=stopped status=%r\n", Status));
}
