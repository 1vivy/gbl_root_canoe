/** @file
 *  RAM-only edits to the live minidump table.
 *
 *  Two edits, both reversible by the next reboot (XBL rebuilds the table
 *  every boot, so a bad edit cannot persist):
 *
 *  - MdTableAppendRegion: add a region entry in the zero slack directly
 *    after an array's last used entry and bump the owning ToC's
 *    region_count. Appending into a subsystem whose encryption_required is
 *    clear makes the region dump plaintext - including when the entry's
 *    address aliases a region another subsystem encrypts, because the dump
 *    transport encrypts per subsystem policy, not per address.
 *  - MdTableSetEncryptionRequired: clear (or set) the subsystem policy word
 *    itself.
 *
 *  Every write is followed by a cache clean: the 900e collector is PBL code
 *  reading DRAM with caches off, and a dirty cache line would hide the edit
 *  from it.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>

#include "MdTableLibInternal.h"

/** True when the 40 bytes at Address are all zero (usable array slack). */
STATIC
BOOLEAN
MdSlackIsZero (
  IN EFI_PHYSICAL_ADDRESS Address
  )
{
  UINT64 Slot[MD_REGION_ENTRY_SIZE / sizeof (UINT64)];

  CopyMem (Slot, (VOID *)(UINTN)Address, sizeof (Slot));
  return (BOOLEAN)((Slot[0] | Slot[1] | Slot[2] | Slot[3] | Slot[4]) == 0);
}

EFI_STATUS
MdTableAppendRegion (
  IN OUT MD_TABLE_MAP   *Map,
  IN     UINTN          ArrayIndex,
  IN     CONST CHAR8    *Name,
  IN     UINT64         Address,
  IN     UINT64         Size,
  OUT    UINTN          *AppendedIndex OPTIONAL
  )
{
  MD_REGION_ARRAY      *Array;
  MD_REGION_ENTRY      Entry;
  EFI_PHYSICAL_ADDRESS Where;
  UINTN                Length;
  UINT32               *CountField;
  UINTN                Index;

  if (Map == NULL || Name == NULL || ArrayIndex >= Map->ArrayCount) {
    return EFI_INVALID_PARAMETER;
  }
  Length = AsciiStrLen (Name);
  if (Length == 0 || Length > MD_REGION_NAME_LEN ||
      Address < MD_MIN_REGION_ADDRESS || Address >= MD_MAX_REGION_ADDRESS ||
      Size == 0 || Size > MD_MAX_REGION_SIZE) {
    return EFI_INVALID_PARAMETER;
  }
  Array = &Map->Arrays[ArrayIndex];
  if (Array->Count >= MD_MAX_ARRAY_ENTRIES) {
    return EFI_OUT_OF_RESOURCES;
  }
  Where = Array->Base + (EFI_PHYSICAL_ADDRESS)Array->Count *
          MD_REGION_ENTRY_SIZE;
  if (Where > MAX_UINTN || !MdSlackIsZero (Where)) {
    /* No visible slack: appending would overwrite live firmware data. */
    return EFI_BAD_BUFFER_SIZE;
  }

  ZeroMem (&Entry, sizeof (Entry));
  for (Index = 0; Index < Length; Index++) {
    Entry.Name[Index] = Name[Index];
  }
  Entry.SeqNum = 0;
  Entry.Valid = MD_REGION_VALID_VALUE;
  Entry.Address = Address;
  Entry.Size = Size;
  CopyMem ((VOID *)(UINTN)Where, &Entry, sizeof (Entry));
  WriteBackInvalidateDataCacheRange ((VOID *)(UINTN)Where,
                                     sizeof (MD_REGION_ENTRY));

  /* Bump the owning subsystem ToC when we know where it lives. */
  if (Array->SubsystemToc != 0) {
    CountField = (UINT32 *)(UINTN)(Array->SubsystemToc +
                                   OFFSET_OF (MD_SUBSYSTEM_TOC, RegionCount));
    *CountField = *CountField + 1;
    WriteBackInvalidateDataCacheRange ((VOID *)CountField, sizeof (UINT32));
    Array->TocRegionCount = *CountField;
  }
  Array->Count++;
  if (AppendedIndex != NULL) {
    *AppendedIndex = Array->Count - 1;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
MdTableSetEncryptionRequired (
  IN OUT MD_TABLE_MAP *Map,
  IN     UINTN        ArrayIndex,
  IN     UINT32       Value,
  OUT    UINT32       *Previous OPTIONAL
  )
{
  MD_REGION_ARRAY *Array;
  UINT32          *Field;

  if (Map == NULL || ArrayIndex >= Map->ArrayCount) {
    return EFI_INVALID_PARAMETER;
  }
  Array = &Map->Arrays[ArrayIndex];
  if (Array->SubsystemToc == 0) {
    return EFI_NOT_FOUND;
  }
  Field = (UINT32 *)(UINTN)(Array->SubsystemToc +
                            OFFSET_OF (MD_SUBSYSTEM_TOC, EncryptionRequired));
  if (Previous != NULL) {
    *Previous = *Field;
  }
  *Field = Value;
  WriteBackInvalidateDataCacheRange ((VOID *)Field, sizeof (UINT32));
  Array->EncryptionRequired = *Field;
  return (*Field == Value) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}
