/*
 * The OEM boot-failure applet's reset timer, and how to switch it off.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SUPER_FB_OEM_WATCHDOG_H__
#define __SUPER_FB_OEM_WATCHDOG_H__

#include <Uefi.h>

/*
 * Some vendors ship a DXE driver that arms its own reset timer before the boot
 * loader runs. On the SM8850 target this was measured on the applet is called
 * Phoenix, and it creates an EVT_TIMER | EVT_NOTIFY_SIGNAL event with a 60 s
 * relative expiry whose notify function calls ResetSystem; it also counts
 * consecutive expiries, and after a few of them diverts the next boot to
 * recovery.
 *
 * It is NOT the UEFI software watchdog. gBS->SetWatchdogTimer () cannot touch
 * it, because the timer is a private EFI_EVENT owned by that driver - which is
 * why an interactive BDS that disables the architectural watchdog still gets
 * reset out from under the operator on such a device.
 *
 * The BDS cannot reach that event directly. The driver does, however, publish a
 * protocol whose fourth member cancels the timer and closes the event: the same
 * primitive the applet uses on its own success path. So the disable is one
 * protocol call, and a device without the applet simply has no such protocol.
 *
 * That makes this a soft failure by construction, at three independent points,
 * none of them fatal to the boot:
 *
 *   1. the protocol is absent            -> this device has no such watchdog
 *   2. the revision is not the decoded one -> unknown layout, do not call
 *   3. the call itself returns an error  -> reported, boot continues
 *
 * ABI, decoded from the driver that publishes the GUID and cross-checked
 * against the vendor loader that consumes it (which locates it exactly this
 * way, unguarded, behind a one-shot flag of its own):
 *
 *   +0x00  UINT64      Revision                observed 1
 *   +0x08  VOID       *ReportBootFailure       expiry counters, recovery divert
 *   +0x10  VOID       *AppendKernelCmdline     adds the applet's cmdline keys
 *   +0x18  EFI_STATUS (*StopWatchdog)(VOID)    SetTimer (ev, Cancel, 0), then
 *                                              CloseEvent (ev)
 *   +0x20  VOID       *StartWatchdog           re-arms the relative expiry
 *
 * Members we never call are kept as VOID * so the offsets stay honest.
 *
 * StopWatchdog reads no arguments and returns the SetTimer status, then closes
 * the event unconditionally. Calling it when the timer was never armed is
 * harmless: the driver's event handle is NULL and both boot services reject
 * NULL. Calling it TWICE is not - the second CloseEvent would run against a
 * freed event - so the disable is strictly one-shot per boot.
 */
typedef struct _SFB_OEM_WDOG_PROTOCOL SFB_OEM_WDOG_PROTOCOL;

typedef EFI_STATUS (EFIAPI *SFB_OEM_WDOG_STOP) (VOID);

struct _SFB_OEM_WDOG_PROTOCOL {
  UINT64             Revision;
  VOID              *ReportBootFailure;
  VOID              *AppendKernelCmdline;
  SFB_OEM_WDOG_STOP  StopWatchdog;
  VOID              *StartWatchdog;
};

/* The only revision whose layout above has been decoded. A different value
 * means the struct may not be this shape, so nothing is called. */
#define SFB_OEM_WDOG_REVISION_DECODED  1ULL

/*
 * Cancel the OEM applet's reset timer if this device has one. Never fails the
 * caller and never blocks the boot; the outcome is reported as a
 * "SFB: MARK oem-wdog" line. Idempotent - repeat calls do nothing.
 */
VOID
SfbOemWatchdogDisable (VOID);

#endif /* __SUPER_FB_OEM_WATCHDOG_H__ */
