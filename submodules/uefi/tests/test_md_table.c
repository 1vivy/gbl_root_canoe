/*
 * Host contracts for bounded Qualcomm minidump discovery.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#undef NULL

#include "../edk2/AndroidToolsPkg/Include/MdTable.h"

VOID *EFIAPI
ZeroMem (VOID *Buffer, UINTN Size)
{
  return memset (Buffer, 0, Size);
}

VOID *EFIAPI
CopyMem (VOID *Destination, CONST VOID *Source, UINTN Size)
{
  return memcpy (Destination, Source, Size);
}

INTN EFIAPI
CompareMem (CONST VOID *First, CONST VOID *Second, UINTN Size)
{
  return memcmp (First, Second, Size);
}

static void
SetLiveToc (
  MD_SUBSYSTEM_TOC *Toc,
  UINT32            Status,
  UINT32            Required,
  MD_REGION_ENTRY  *Regions,
  UINT32            Count
  )
{
  ZeroMem (Toc, sizeof (*Toc));
  Toc->Status = Status;
  Toc->Enabled = MD_SS_ENABLED_VALUE;
  Toc->EncryptionStatus = MD_SS_ENCR_DONE_VALUE;
  Toc->EncryptionRequired = Required;
  Toc->RegionCount = Count;
  Toc->RegionsBasePtr = (UINT64)(UINTN)Regions;
}

static void
TestMeasuredRootLayout (void)
{
  assert (sizeof (MD_GTOC_HEADER) == MD_GTOC_HEADER_SIZE);
  assert (sizeof (MD_SUBSYSTEM_TOC) == MD_SUBSYSTEM_TOC_SIZE);
  assert (offsetof (MD_GLOBAL_TOC, Subsystems) == MD_GTOC_HEADER_SIZE);
  assert (sizeof (MD_GLOBAL_TOC) == MD_GTOC_SIZE_BYTES);
  assert (sizeof (MD_GLOBAL_TOC) == 944);
}

static void
TestDirectRootMapsOnlyRequiredSubsystems (void)
{
  MD_GLOBAL_TOC   Root;
  MD_REGION_ENTRY Aop[16];
  MD_REGION_ENTRY Boot[8];
  MD_TABLE_MAP    Map;

  ZeroMem (&Root, sizeof (Root));
  ZeroMem (Aop, sizeof (Aop));
  ZeroMem (Boot, sizeof (Boot));
  Root.Header.Status = 1;
  Root.Header.Revision = MD_GTOC_REVISION;
  Root.Header.Enabled = MD_SS_ENABLED_VALUE;
  SetLiveToc (&Root.Subsystems[MD_SS_AOP], MD_SS_AOP_TOC_MAGIC_VALUE,
              MD_SS_ENCR_NOTREQ_VALUE, Aop, 16);
  SetLiveToc (&Root.Subsystems[MD_SS_BOOT], MD_SS_TOC_MAGIC_VALUE,
              MD_SS_ENCR_REQ_VALUE, Boot, 8);
  SetLiveToc (&Root.Subsystems[1], MD_SS_TOC_MAGIC_VALUE,
              MD_SS_ENCR_REQ_VALUE, Boot, 1);

  assert (MdTableMapRoot ((EFI_PHYSICAL_ADDRESS)(UINTN)&Root, sizeof (Root),
                          &Map) == EFI_SUCCESS);
  assert (Map.GtocAddress == (EFI_PHYSICAL_ADDRESS)(UINTN)&Root);
  assert (Map.GtocFromSmem);
  assert (Map.SubsystemCount == MD_MAX_SUBSYSTEMS);
  assert (Map.ArrayCount == 2);
  assert (Map.Arrays[0].SubsystemIndex == MD_SS_AOP);
  assert (Map.Arrays[0].Base == (EFI_PHYSICAL_ADDRESS)(UINTN)Aop);
  assert (Map.Arrays[0].Count == 16);
  assert (Map.Arrays[0].EncryptionRequired == MD_SS_ENCR_NOTREQ_VALUE);
  assert (Map.Arrays[1].SubsystemIndex == MD_SS_BOOT);
  assert (Map.Arrays[1].Base == (EFI_PHYSICAL_ADDRESS)(UINTN)Boot);
  assert (Map.Arrays[1].Count == 8);
  assert (Map.Arrays[1].EncryptionRequired == MD_SS_ENCR_REQ_VALUE);
}

static void
TestDirectRootRejectsWrongGeometryAndRevision (void)
{
  MD_GLOBAL_TOC Root;
  MD_TABLE_MAP  Map;

  ZeroMem (&Root, sizeof (Root));
  Root.Header.Status = 1;
  Root.Header.Revision = MD_GTOC_REVISION;
  assert (MdTableMapRoot ((EFI_PHYSICAL_ADDRESS)(UINTN)&Root,
                          sizeof (Root) - 1, &Map) == EFI_BAD_BUFFER_SIZE);

  Root.Header.Revision = MD_GTOC_REVISION + 1;
  assert (MdTableMapRoot ((EFI_PHYSICAL_ADDRESS)(UINTN)&Root,
                          sizeof (Root), &Map) == EFI_COMPROMISED_DATA);
}

static void
TestRootMappingDoesNotDereferenceRegionArrays (void)
{
  MD_GLOBAL_TOC Root;
  MD_TABLE_MAP  Map;

  ZeroMem (&Root, sizeof (Root));
  Root.Header.Status = 1;
  Root.Header.Revision = MD_GTOC_REVISION;
  SetLiveToc (&Root.Subsystems[MD_SS_AOP], MD_SS_AOP_TOC_MAGIC_VALUE,
              MD_SS_ENCR_NOTREQ_VALUE, (MD_REGION_ENTRY *)(UINTN)1, 1);
  SetLiveToc (&Root.Subsystems[MD_SS_BOOT], MD_SS_TOC_MAGIC_VALUE,
              MD_SS_ENCR_REQ_VALUE, (MD_REGION_ENTRY *)(UINTN)2, 1);

  assert (MdTableMapRoot ((EFI_PHYSICAL_ADDRESS)(UINTN)&Root, sizeof (Root),
                          &Map) == EFI_SUCCESS);
  assert (Map.ArrayCount == 2);
  assert (Map.Arrays[0].Base == 1);
  assert (Map.Arrays[1].Base == 2);
}

static void
TestFreeSlotPrefersSpaceAboveHighestUsed (void)
{
  MD_SUBSYSTEM_TOC Slots[MD_MAX_SUBSYSTEMS];
  UINTN            Index;
  UINTN            Highest;
  BOOLEAN          AboveHighest;
  BOOLEAN          AnyUsed;

  ZeroMem (Slots, sizeof (Slots));
  Slots[0].Status = 1;
  Slots[4].Status = 1;
  assert (MdTableSelectFreeSubsystem (Slots, MD_MAX_SUBSYSTEMS, &Index,
                                      &AboveHighest, &AnyUsed, &Highest) ==
          EFI_SUCCESS);
  assert (AnyUsed);
  assert (Highest == 4);
  assert (AboveHighest);
  assert (Index == 5);
}

static void
TestFreeSlotFallsBackBelowAUsedTail (void)
{
  MD_SUBSYSTEM_TOC Slots[MD_MAX_SUBSYSTEMS];
  UINTN            Index;
  UINTN            Highest;
  BOOLEAN          AboveHighest;
  BOOLEAN          AnyUsed;
  UINTN            Slot;

  ZeroMem (Slots, sizeof (Slots));
  for (Slot = 0; Slot < MD_MAX_SUBSYSTEMS; Slot++) {
    Slots[Slot].Status = 1;
  }
  ZeroMem (&Slots[2], sizeof (Slots[2]));
  assert (MdTableSelectFreeSubsystem (Slots, MD_MAX_SUBSYSTEMS, &Index,
                                      &AboveHighest, &AnyUsed, &Highest) ==
          EFI_SUCCESS);
  assert (AnyUsed);
  assert (Highest == MD_MAX_SUBSYSTEMS - 1);
  assert (!AboveHighest);
  assert (Index == 2);
}

static void
TestFreeSlotRefusesAFullTable (void)
{
  MD_SUBSYSTEM_TOC Slots[MD_MAX_SUBSYSTEMS];
  UINTN            Index;
  UINTN            Highest;
  BOOLEAN          AboveHighest;
  BOOLEAN          AnyUsed;
  UINTN            Slot;

  ZeroMem (Slots, sizeof (Slots));
  for (Slot = 0; Slot < MD_MAX_SUBSYSTEMS; Slot++) {
    Slots[Slot].Status = 1;
  }
  assert (MdTableSelectFreeSubsystem (Slots, MD_MAX_SUBSYSTEMS, &Index,
                                      &AboveHighest, &AnyUsed, &Highest) ==
          EFI_NOT_FOUND);
}

int
main (void)
{
  TestMeasuredRootLayout ();
  TestDirectRootMapsOnlyRequiredSubsystems ();
  TestDirectRootRejectsWrongGeometryAndRevision ();
  TestRootMappingDoesNotDereferenceRegionArrays ();
  TestFreeSlotPrefersSpaceAboveHighestUsed ();
  TestFreeSlotFallsBackBelowAUsedTail ();
  TestFreeSlotRefusesAFullTable ();
  puts ("md table tests passed");
  return 0;
}
