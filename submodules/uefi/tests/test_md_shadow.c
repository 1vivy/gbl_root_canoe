/*
 * Host contracts for interactive minidump shadow-target enumeration.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#undef NULL

#include "../edk2/AndroidToolsPkg/Application/MdTools/MdShadow.h"

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

static UINTN      ReadCalls;
static UINTN      FailArray = MAX_UINTN;
static UINTN      FailEntry = MAX_UINTN;
static EFI_STATUS ReadFailure = EFI_DEVICE_ERROR;

EFI_STATUS
MdTableReadEntry (
  IN  CONST MD_TABLE_MAP *Map,
  IN  UINTN               ArrayIndex,
  IN  UINTN               EntryIndex,
  OUT MD_REGION_ENTRY    *Entry
  )
{
  CONST MD_REGION_ENTRY *Entries;

  ++ReadCalls;
  if (ArrayIndex == FailArray && EntryIndex == FailEntry) {
    return ReadFailure;
  }
  if (Map == NULL || Entry == NULL || ArrayIndex >= Map->ArrayCount ||
      EntryIndex >= Map->Arrays[ArrayIndex].Count) {
    return EFI_INVALID_PARAMETER;
  }
  Entries = (CONST MD_REGION_ENTRY *)(UINTN)Map->Arrays[ArrayIndex].Base;
  memcpy (Entry, &Entries[EntryIndex], sizeof (*Entry));
  return EFI_SUCCESS;
}

static void
SetEntry (
  MD_REGION_ENTRY *Entry,
  const char      *Name,
  UINT32           Sequence,
  UINT64           Address,
  UINT64           Size
  )
{
  memset (Entry, 0, sizeof (*Entry));
  size_t Length = strlen (Name);

  memcpy (Entry->Name, Name,
          Length < sizeof (Entry->Name) ? Length : sizeof (Entry->Name));
  Entry->SeqNum = Sequence;
  Entry->Valid = MD_REGION_VALID_VALUE;
  Entry->Address = Address;
  Entry->Size = Size;
}

static void
SetArray (
  MD_REGION_ARRAY *Array,
  UINTN            SubsystemIndex,
  MD_REGION_ENTRY *Entries,
  UINT32           Count,
  UINT32           EncryptionRequired
  )
{
  memset (Array, 0, sizeof (*Array));
  Array->Base = (EFI_PHYSICAL_ADDRESS)(UINTN)Entries;
  Array->Count = Count;
  Array->SubsystemIndex = SubsystemIndex;
  Array->EncryptionRequired = EncryptionRequired;
}

static void
BuildMap (
  MD_TABLE_MAP    *Map,
  MD_REGION_ENTRY  Aop[2],
  MD_REGION_ENTRY  Boot[2]
  )
{
  memset (Map, 0, sizeof (*Map));
  SetEntry (&Aop[0], "AOP-A", 2, 0x90000000, 0x1000);
  SetEntry (&Aop[1], "AOP-B", 3, 0x90001000, 0x2000);
  SetEntry (&Boot[0], "BOOT-A", 7, 0xA0000000, 0x3000);
  SetEntry (&Boot[1], "BOOT-B", 8, 0xA0003000, 0x4000);
  Map->ArrayCount = 2;
  SetArray (&Map->Arrays[0], MD_SS_AOP, Aop, 2,
            MD_SS_ENCR_NOTREQ_VALUE);
  SetArray (&Map->Arrays[1], MD_SS_BOOT, Boot, 2,
            MD_SS_ENCR_REQ_VALUE);
}

static void
TestEnumeratesEveryValidatedTargetInArrayOrder (void)
{
  MD_REGION_ENTRY  Aop[2];
  MD_REGION_ENTRY  Boot[2];
  MD_TABLE_MAP     Map;
  MD_SHADOW_TARGET Targets[MD_SHADOW_MAX_TARGETS];
  UINTN            Count;

  BuildMap (&Map, Aop, Boot);
  ReadCalls = 0;
  assert (MdShadowCollectTargets (&Map, Targets, MD_SHADOW_MAX_TARGETS,
                                  &Count) == EFI_SUCCESS);
  assert (Count == 4);
  assert (ReadCalls == 4);

  assert (Targets[0].ArrayIndex == 0);
  assert (Targets[0].EntryIndex == 0);
  assert (Targets[0].SubsystemIndex == MD_SS_AOP);
  assert (Targets[0].EncryptionRequired == MD_SS_ENCR_NOTREQ_VALUE);
  assert (strcmp (Targets[0].Alias, "SM8850-A00") == 0);
  assert (memcmp (&Targets[0].Source, &Aop[0], sizeof (Aop[0])) == 0);

  assert (Targets[2].ArrayIndex == 1);
  assert (Targets[2].EntryIndex == 0);
  assert (Targets[2].SubsystemIndex == MD_SS_BOOT);
  assert (Targets[2].EncryptionRequired == MD_SS_ENCR_REQ_VALUE);
  assert (strcmp (Targets[2].Alias, "SM8850-B00") == 0);
  assert (strcmp (Targets[3].Alias, "SM8850-B01") == 0);
  assert (memcmp (&Targets[3].Source, &Boot[1], sizeof (Boot[1])) == 0);
}

static void
TestBuildsAliasEntryOverTheExistingPayload (void)
{
  MD_REGION_ENTRY  Aop[2];
  MD_REGION_ENTRY  Boot[2];
  MD_REGION_ENTRY  Region;
  MD_TABLE_MAP     Map;
  MD_SHADOW_TARGET Targets[MD_SHADOW_MAX_TARGETS];
  UINTN            Count;

  BuildMap (&Map, Aop, Boot);
  assert (MdShadowCollectTargets (&Map, Targets, MD_SHADOW_MAX_TARGETS,
                                  &Count) == EFI_SUCCESS);
  assert (MdShadowBuildRegion (&Targets[3], &Region) == EFI_SUCCESS);
  assert (strcmp (Region.Name, "SM8850-B01") == 0);
  assert (Region.SeqNum == 0);
  assert (Region.Valid == MD_REGION_VALID_VALUE);
  assert (Region.Address == Boot[1].Address);
  assert (Region.Size == Boot[1].Size);
}

static void
TestReportsRequiredCapacityWithoutPartialOutput (void)
{
  MD_REGION_ENTRY  Aop[2];
  MD_REGION_ENTRY  Boot[2];
  MD_TABLE_MAP     Map;
  MD_SHADOW_TARGET Targets[3];
  MD_SHADOW_TARGET Before[3];
  UINTN            Count;

  BuildMap (&Map, Aop, Boot);
  memset (Targets, 0xA5, sizeof (Targets));
  memcpy (Before, Targets, sizeof (Before));
  ReadCalls = 0;
  Count = 0;
  assert (MdShadowCollectTargets (&Map, Targets, 3, &Count) ==
          EFI_BUFFER_TOO_SMALL);
  assert (Count == 4);
  assert (ReadCalls == 0);
  assert (memcmp (Targets, Before, sizeof (Targets)) == 0);
}

static void
TestRefusesMalformedMapsAndReadFailures (void)
{
  MD_REGION_ENTRY  Aop[2];
  MD_REGION_ENTRY  Boot[2];
  MD_TABLE_MAP     Map;
  MD_SHADOW_TARGET Targets[MD_SHADOW_MAX_TARGETS];
  UINTN            Count;

  BuildMap (&Map, Aop, Boot);
  Map.Arrays[0].Count = MD_MAX_APPEND_REGIONS + 1;
  ReadCalls = 0;
  assert (MdShadowCollectTargets (&Map, Targets, MD_SHADOW_MAX_TARGETS,
                                  &Count) == EFI_COMPROMISED_DATA);
  assert (Count == 0);
  assert (ReadCalls == 0);

  BuildMap (&Map, Aop, Boot);
  Map.Arrays[1].SubsystemIndex = 3;
  assert (MdShadowCollectTargets (&Map, Targets, MD_SHADOW_MAX_TARGETS,
                                  &Count) == EFI_COMPROMISED_DATA);
  assert (Count == 0);

  BuildMap (&Map, Aop, Boot);
  FailArray = 1;
  FailEntry = 1;
  ReadCalls = 0;
  assert (MdShadowCollectTargets (&Map, Targets, MD_SHADOW_MAX_TARGETS,
                                  &Count) == ReadFailure);
  assert (Count == 0);
  assert (ReadCalls == 4);
  FailArray = MAX_UINTN;
  FailEntry = MAX_UINTN;
}

int
main (void)
{
  TestEnumeratesEveryValidatedTargetInArrayOrder ();
  TestBuildsAliasEntryOverTheExistingPayload ();
  TestReportsRequiredCapacityWithoutPartialOutput ();
  TestRefusesMalformedMapsAndReadFailures ();
  puts ("md shadow tests passed");
  return 0;
}
