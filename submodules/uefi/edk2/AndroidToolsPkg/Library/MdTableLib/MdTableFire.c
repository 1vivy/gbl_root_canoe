/** @file
 *  Fault/reset triggers shared by MdTools and CrashTools.
 *
 *  Each trigger is a primitive only: it performs the action and, when the
 *  action does not take the device down, reports that we survived. A
 *  survived trigger is a finding - it rules that path out for reaching the
 *  900e memory-debug mode on this device.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include <MdTable.h>

/* Vendor reset-data string for emergency download (ported from the r32
   QcomModulePkg ShutdownServices.c: ResetSystem(EfiResetPlatformSpecific,
   "EDL") lands the device in Sahara 9008). */
#define MD_RESET_DATA_EDL  L"EDL"

/* How long to wait after a fault write/read before declaring survival. */
#define MD_TRIGGER_SURVIVAL_SPINS  30u
#define MD_TRIGGER_SPIN_US         100000u

/**
  Write Value to a physical Address expected to be protected (e.g. the
  secure-world DDR carveout, which an XPU should refuse from the non-secure
  side). Returns only when the device did NOT fault; *Readback reports what
  the address holds afterwards (a dropped write reads back stale).
**/
VOID
MdTriggerWriteFault (
  IN  UINT64 Address,
  IN  UINT32 Value,
  OUT UINT32 *Readback
  )
{
  volatile UINT32 *Target;
  UINTN           Spin;

  Target = (volatile UINT32 *)(UINTN)Address;
  *Target = Value;
  WriteBackInvalidateDataCacheRange ((VOID *)Target, sizeof (UINT32));
  for (Spin = 0; Spin < MD_TRIGGER_SURVIVAL_SPINS; Spin++) {
    gBS->Stall (MD_TRIGGER_SPIN_US);
  }
  if (Readback != NULL) {
    *Readback = *Target;
  }
}

/** Read a physical Address expected to be unmapped. Returns iff no abort. */
VOID
MdTriggerReadFault (
  IN UINT64 Address
  )
{
  volatile UINT32 Value;

  Value = *(volatile UINT32 *)(UINTN)Address;
  (VOID)Value;
}

/** Raise a synchronous BRK exception. Returns iff the handler resumed us. */
VOID
MdTriggerBreak (
  VOID
  )
{
  __builtin_trap ();
}

/** Vendor emergency-download reset (control: 9008, not the 900e dump). */
VOID
MdTriggerEdlReset (
  VOID
  )
{
  gRT->ResetSystem (EfiResetPlatformSpecific, EFI_SUCCESS,
                    StrSize (MD_RESET_DATA_EDL), (VOID *)MD_RESET_DATA_EDL);
}
