/** @file
 *  UsbTools - USB host-mode diagnostics for the Canoe boot environment.
 *
 *  Answers, from the device itself, the questions that decide whether USB
 *  host boot is reachable: what the resident firmware dispatched, what the
 *  UsbConfig cores claim they can do, what a host-mode attempt actually
 *  produces, and where in that attempt the chain stops.
 *
 *  The census is read-only. The host-mode attempt writes controller state
 *  through the vendor UsbConfig protocol and is separately confirmed; it
 *  always restores device mode before returning, using the vendor-ordered
 *  single StartController(DEVICE_SS) - a naked StopController faulted this
 *  device class twice.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __USB_TOOLS_H__
#define __USB_TOOLS_H__

#include <Uefi.h>
#include <AndroidToolsUi.h>

/* Report row capacity for the census and the attempt transcript. */
#define UT_CENSUS_ROWS  96u
#define UT_ATTEMPT_ROWS 128u

/*
 * The last attempt's transcript, kept so the logfs dump can write it after
 * the fact. UtAttemptRan is FALSE until the first attempt completes or
 * fails out.
 */
extern AT_REPORT  mUtAttemptReport;
extern BOOLEAN    mUtAttemptRan;

/* Passive census: dispatched USB-relevant protocols, every UsbConfig
 * instance with its revision/core/mode, per-core capability, VBUS state and
 * vtable member presence. */
EFI_STATUS
UtBuildCensusReport (OUT AT_REPORT *Report);

/*
 * The confirmed active probe: capability gate, driver stack load from the
 * boot root, vendor-ordered host start, bind of only the handles the start
 * created, enumeration wait, verdict, and device-mode restore. Every step
 * lands in the transcript and on the screen, so a fault leaves the stage
 * name on the display.
 */
EFI_STATUS
UtRunHostAttempt (IN EFI_HANDLE ImageHandle);

/* Write the census and the last attempt transcript to logfs. */
EFI_STATUS
UtDumpToLogfs (VOID);

#endif
