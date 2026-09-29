/*
 * Host contract for durable stage ordering around bounded discovery.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#undef NULL

#include "../edk2/AndroidToolsPkg/Application/MdTools/MdTools.h"

static char       Events[32];
static size_t     EventCount;
static BOOLEAN    FailNextFlush;
static unsigned   LocateCalls;
static unsigned   ScanCalls;
static EFI_STATUS ScanResult = EFI_ACCESS_DENIED;

static void
PushEvent (char Event)
{
  assert (EventCount + 1 < sizeof (Events));
  Events[EventCount++] = Event;
  Events[EventCount] = '\0';
}

static void
ResetEvents (void)
{
  EventCount = 0;
  Events[0] = '\0';
}

EFI_STATUS
MdEvidencePrint (
  IN OUT MD_EVIDENCE *Evidence,
  IN     CONST CHAR16 *Format,
  ...
  )
{
  assert (Evidence != NULL);
  assert (Evidence->Open);
  assert (Format != NULL);
  PushEvent ('P');
  return EFI_SUCCESS;
}

EFI_STATUS
MdEvidenceFlush (
  IN OUT MD_EVIDENCE *Evidence
  )
{
  assert (Evidence != NULL);
  assert (Evidence->Open);
  PushEvent ('F');
  if (FailNextFlush) {
    FailNextFlush = FALSE;
    return EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
MdTableLocateRoot (
  OUT EFI_PHYSICAL_ADDRESS *Address,
  OUT UINTN                *Bytes
  )
{
  PushEvent ('L');
  ++LocateCalls;
  *Address = 0x1000;
  *Bytes = sizeof (MD_GLOBAL_TOC);
  return EFI_SUCCESS;
}

EFI_STATUS
MdTableScanRoot (
  IN  EFI_PHYSICAL_ADDRESS Address,
  IN  UINTN                Bytes,
  OUT MD_TABLE_MAP         *Map
  )
{
  PushEvent ('S');
  ++ScanCalls;
  assert (Address == 0x1000);
  assert (Bytes == sizeof (MD_GLOBAL_TOC));
  memset (Map, 0, sizeof (*Map));
  return ScanResult;
}

int
main (void)
{
  MD_EVIDENCE Evidence;
  EFI_STATUS  Status;

  memset (&Evidence, 0, sizeof (Evidence));
  assert (MdEnsureScan (&Evidence) == EFI_INVALID_PARAMETER);
  assert (Events[0] == '\0');

  Evidence.Open = TRUE;
  FailNextFlush = TRUE;
  ResetEvents ();
  Status = MdEnsureScan (&Evidence);
  assert (Status == EFI_DEVICE_ERROR);
  assert (strcmp (Events, "PF") == 0);
  assert (LocateCalls == 0);
  assert (ScanCalls == 0);
  assert (MdCachedMap () == NULL);

  ResetEvents ();
  Status = MdEnsureScan (&Evidence);
  assert (Status == EFI_ACCESS_DENIED);
  assert (strcmp (Events, "PFLPFPFSPF") == 0);
  assert (LocateCalls == 1);
  assert (ScanCalls == 1);
  assert (MdCachedMap () != NULL);

  ResetEvents ();
  Status = MdEnsureScan (&Evidence);
  assert (Status == EFI_ACCESS_DENIED);
  assert (strcmp (Events, "PF") == 0);
  assert (LocateCalls == 1);
  assert (ScanCalls == 1);

  puts ("md discovery tests passed");
  return 0;
}
