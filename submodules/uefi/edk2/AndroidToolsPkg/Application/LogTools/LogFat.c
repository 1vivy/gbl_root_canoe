/** @file
 *  Bind the FAT stack this tool needs to write its report.
 *
 *  This application is meant to be launched directly by the ABL, before the
 *  BDS runs, because the BDS's own output is the thing most likely to have
 *  displaced the ring contents being measured. Nothing has connected the log
 *  partition to a filesystem driver at that point: enumerating
 *  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL finds no handle for it, and the report has
 *  nowhere to go. Connecting every handle once is what makes the drivers bind.
 *
 *  Deliberately unconditional rather than aimed at a named partition: the
 *  handle that will carry logfs does not advertise a filesystem until after a
 *  driver has been given the chance to claim it, so there is nothing to match
 *  on beforehand. Failures are expected and ignored - most handles on this
 *  platform are not block devices and have no driver that wants them.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "LogTools.h"

VOID
LtStartFatStack (
  VOID
  )
{
  STATIC BOOLEAN Started = FALSE;
  EFI_HANDLE    *Handles;
  UINTN          Count;
  UINTN          Index;
  EFI_STATUS     Status;

  /* Repeating the pass cannot bind anything new and costs a full connect
     sweep, so the first attempt is the only one. */
  if (Started) {
    return;
  }
  Started = TRUE;

  Handles = NULL;
  Count = 0;
  Status = gBS->LocateHandleBuffer (AllHandles, NULL, NULL, &Count, &Handles);
  if (EFI_ERROR (Status) || Handles == NULL) {
    return;
  }
  for (Index = 0; Index < Count; Index++) {
    gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
  }
  FreePool (Handles);
}
