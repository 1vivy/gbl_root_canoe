/*
 * Persistent settings for the super-fastboot boot menu.
 *
 * The firmware refuses EFI variables it does not already know about, so the
 * menu keeps its settings in the EFI System Partition instead. Only the last
 * megabyte of that partition is safe to write, so the store uses permanent
 * 1 KiB NUL-padded ASCII records measured from the partition end:
 *
 *   [ ... file system ... | 1 MiB scratch ... | mode | default | custom ] end
 *
 * Nothing here goes through the file system: the records must survive the ESP
 * being written by an operating system that knows nothing about them, and a
 * file would not.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbConfig.h"
#include "SuperFbMenu.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Guid/Gpt.h>
#include <Protocol/BlockIo.h>
#include <Protocol/PartitionInfo.h>

/* Keeps the translation unit legal when the feature is compiled out. */
CONST CHAR8 *gSfbStoreModuleTag = "SuperFbStore";


STATIC_ASSERT (SFB_STORE_MODE_TAIL_DISTANCE <= SFB_STORE_SCRATCH_BYTES,
               "SuperFb mode record must remain within scratch area");

/* Refuse anything too small to have the megabyte of slack we were promised. */
#define SFB_STORE_MIN_PARTITION_BYTES  SIZE_1MB

/* Sanity bounds on a GPT header before its contents are believed. */
#define SFB_GPT_MAX_ENTRIES     512
#define SFB_GPT_MIN_ENTRY_SIZE  128
#define SFB_GPT_MAX_ENTRY_SIZE  4096
#define SFB_STORE_MAX_BLOCK_BYTES SIZE_1MB

STATIC
BOOLEAN
SfbMulU64 (IN UINT64 Left, IN UINT64 Right, OUT UINT64 *Result)
{
  if (Result == NULL || (Left != 0 && Right > MAX_UINT64 / Left)) {
    return FALSE;
  }
  *Result = Left * Right;
  return TRUE;
}

STATIC
BOOLEAN
SfbValidBlockMedia (IN CONST EFI_BLOCK_IO_MEDIA *Media)
{
  if (Media == NULL || Media->BlockSize == 0 ||
      Media->BlockSize > SFB_STORE_MAX_BLOCK_BYTES) {
    return FALSE;
  }
  return (BOOLEAN)(Media->IoAlign <= 1 ||
                   (Media->IoAlign & (Media->IoAlign - 1)) == 0);
}


typedef struct {
  EFI_BLOCK_IO_PROTOCOL  *BlockIo;
  /* Exclusive byte offset immediately after the ESP on BlockIo's media. */
  UINT64                 PartitionEnd;
  BOOLEAN                Resolved;
  BOOLEAN                Failed;
} SFB_STORE_LOCATION;

STATIC SFB_STORE_LOCATION  mSfbStore = { NULL, 0, FALSE, FALSE };

/* ---- finding the EFI System Partition ----------------------------------- */

/*
 * Read Blocks starting at Lba into a freshly allocated, IoAlign-correct buffer.
 * Caller releases it with FreeAlignedPages () and the same page count.
 */
STATIC
EFI_STATUS
SfbReadBlocks (IN EFI_BLOCK_IO_PROTOCOL  *BlockIo,
               IN EFI_LBA                Lba,
               IN UINTN                  Blocks,
               OUT VOID                  **Buffer,
               OUT UINTN                 *Pages)
{
  EFI_STATUS  Status;
  UINTN       Bytes;
  UINTN       Alignment;

  if (BlockIo == NULL || !SfbValidBlockMedia (BlockIo->Media) ||
      Buffer == NULL || Pages == NULL || Blocks == 0 ||
      Lba > BlockIo->Media->LastBlock ||
      Blocks - 1 > BlockIo->Media->LastBlock - Lba ||
      Blocks > MAX_UINTN / BlockIo->Media->BlockSize) {
    return EFI_INVALID_PARAMETER;
  }

  *Buffer = NULL;
  *Pages = 0;

  Bytes = Blocks * BlockIo->Media->BlockSize;
  Alignment = (BlockIo->Media->IoAlign > 1) ? BlockIo->Media->IoAlign : 8;

  *Pages = EFI_SIZE_TO_PAGES (Bytes);
  *Buffer = AllocateAlignedPages (*Pages, Alignment);
  if (*Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId, Lba,
                                Bytes, *Buffer);
  if (EFI_ERROR (Status)) {
    FreeAlignedPages (*Buffer, *Pages);
    *Buffer = NULL;
    *Pages = 0;
  }

  return Status;
}

/* The GPT name the platform gives its EFI System Partition. */
#define SFB_ESP_PARTITION_NAME  L"efisp"

#define SFB_ESP_NO_MATCH    0
#define SFB_ESP_BY_TYPE     1
#define SFB_ESP_BY_NAME     2

/*
 * How well an entry answers to "the ESP". PartitionName is a fixed-width field
 * and is not required to be NUL terminated, so it is copied out before being
 * compared as a string.
 *
 * The NAME is checked before the type GUID on purpose. On this platform the BDS
 * and the mode-record tail live on a vendor `efisp` partition that is NOT typed
 * as an EFI System Partition, so requiring the ESP GUID first would leave the
 * mode store unresolvable on exactly the devices this loader targets. A
 * correctly typed ESP remains the fallback when no such name exists.
 */
STATIC
UINTN
SfbRankEspEntry (IN CONST EFI_PARTITION_ENTRY *Entry)
{
  CHAR16  Name[ARRAY_SIZE (Entry->PartitionName) + 1];

  CopyMem (Name, Entry->PartitionName, sizeof (Entry->PartitionName));
  Name[ARRAY_SIZE (Entry->PartitionName)] = L'\0';

  if (StrCmp (Name, SFB_ESP_PARTITION_NAME) == 0) {
    return SFB_ESP_BY_NAME;
  }

  if (CompareGuid (&Entry->PartitionTypeGUID, &gEfiPartTypeSystemPartGuid)) {
    return SFB_ESP_BY_TYPE;
  }

  return SFB_ESP_NO_MATCH;
}

/*
 * Look for the EFI System Partition in Disk's GPT. On success the partition's
 * last byte, exclusive, is returned as an absolute offset on Disk.
 *
 * The named partition wins over a merely ESP-typed one, so a table holding both
 * still resolves to the one the platform means.
 */
STATIC
EFI_STATUS
SfbFindEspInGpt (IN EFI_BLOCK_IO_PROTOCOL *Disk, OUT UINT64 *PartitionEnd)
{
  EFI_STATUS                   Status;
  EFI_PARTITION_TABLE_HEADER   *Header = NULL;
  UINT8                        *Entries = NULL;
  UINTN                        HeaderPages = 0;
  UINTN                        EntryPages = 0;
  UINTN                        BlockSize;
  UINTN                        EntryBytes;
  UINTN                        EntryBlocks;
  UINTN                        Index;
  UINTN                        Best;

  if (Disk == NULL || PartitionEnd == NULL ||
      !SfbValidBlockMedia (Disk->Media)) {
    return EFI_VOLUME_CORRUPTED;
  }
  BlockSize = Disk->Media->BlockSize;
  if (BlockSize < sizeof (EFI_PARTITION_TABLE_HEADER)) {
    return EFI_VOLUME_CORRUPTED;
  }

  *PartitionEnd = 0;

  Status = SfbReadBlocks (Disk, PRIMARY_PART_HEADER_LBA, 1,
                          (VOID **)&Header, &HeaderPages);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = EFI_NOT_FOUND;

  if (Header->Header.Signature != EFI_PTAB_HEADER_ID ||
      Header->NumberOfPartitionEntries == 0 ||
      Header->NumberOfPartitionEntries > SFB_GPT_MAX_ENTRIES ||
      Header->SizeOfPartitionEntry < SFB_GPT_MIN_ENTRY_SIZE ||
      Header->SizeOfPartitionEntry > SFB_GPT_MAX_ENTRY_SIZE ||
      (Header->SizeOfPartitionEntry % 8) != 0) {
    goto Done;
  }

  EntryBytes = (UINTN)Header->NumberOfPartitionEntries *
               Header->SizeOfPartitionEntry;
  if (EntryBytes > MAX_UINTN - (BlockSize - 1)) {
    goto Done;
  }
  EntryBlocks = (EntryBytes + BlockSize - 1) / BlockSize;

  Status = SfbReadBlocks (Disk, Header->PartitionEntryLBA, EntryBlocks,
                          (VOID **)&Entries, &EntryPages);
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  Status = EFI_NOT_FOUND;
  Best = SFB_ESP_NO_MATCH;

  for (Index = 0; Index < Header->NumberOfPartitionEntries; Index++) {
    CONST EFI_PARTITION_ENTRY  *Entry =
      (CONST EFI_PARTITION_ENTRY *)(Entries +
                                    Index * Header->SizeOfPartitionEntry);
    UINTN  Rank = SfbRankEspEntry (Entry);

    if (Rank <= Best) {
      continue;
    }

    {
      UINT64 PartitionBlocks;
      UINT64 PartitionBytes;
      UINT64 EndLbaExclusive;

      if (Entry->EndingLBA < Entry->StartingLBA ||
          Entry->EndingLBA > Disk->Media->LastBlock ||
          Entry->EndingLBA - Entry->StartingLBA == MAX_UINT64) {
        continue;
      }
      PartitionBlocks = Entry->EndingLBA - Entry->StartingLBA + 1;
      if (!SfbMulU64 (PartitionBlocks, BlockSize, &PartitionBytes) ||
          PartitionBytes < SFB_STORE_MIN_PARTITION_BYTES) {
        DEBUG ((EFI_D_ERROR, "SFB: ESP candidate too small or invalid\n"));
        continue;
      }
      if (Entry->EndingLBA == MAX_UINT64 ||
          !SfbMulU64 (Entry->EndingLBA + 1, BlockSize,
                      &EndLbaExclusive)) {
        continue;
      }

      *PartitionEnd = EndLbaExclusive;
    }
    Status = EFI_SUCCESS;
    Best = Rank;

    /* Nothing outranks the partition the platform actually named. */
    if (Best == SFB_ESP_BY_NAME) {
      break;
    }
  }

Done:
  if (Entries != NULL) {
    FreeAlignedPages (Entries, EntryPages);
  }
  FreeAlignedPages (Header, HeaderPages);

  return Status;
}

/*
 * Second way in, for platforms whose storage stack hands out partitions but no
 * readable GPT: ask the partition handles themselves what they are.
 */
STATIC
EFI_STATUS
SfbFindEspByPartitionInfo (OUT EFI_BLOCK_IO_PROTOCOL **BlockIo,
                           OUT UINT64                *PartitionEnd)
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count = 0;
  UINTN       Index;

  *BlockIo = NULL;
  *PartitionEnd = 0;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiPartitionInfoProtocolGuid,
                                    NULL, &Count, &Handles);
  if (EFI_ERROR (Status) || Handles == NULL) {
    return EFI_NOT_FOUND;
  }

  Status = EFI_NOT_FOUND;

  for (Index = 0; Index < Count; Index++) {
    EFI_PARTITION_INFO_PROTOCOL  *Info = NULL;
    EFI_BLOCK_IO_PROTOCOL        *Candidate = NULL;
    UINT64                       Bytes;

    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index],
                                        &gEfiPartitionInfoProtocolGuid,
                                        (VOID **)&Info)) ||
        Info->Type != PARTITION_TYPE_GPT ||
        !CompareGuid (&Info->Info.Gpt.PartitionTypeGUID,
                      &gEfiPartTypeSystemPartGuid)) {
      continue;
    }

    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index],
                                        &gEfiBlockIoProtocolGuid,
                                        (VOID **)&Candidate)) ||
        Candidate->Media == NULL || !Candidate->Media->MediaPresent ||
        !SfbValidBlockMedia (Candidate->Media) ||
        Candidate->Media->LastBlock == MAX_UINT64 ||
        !SfbMulU64 (Candidate->Media->LastBlock + 1,
                    Candidate->Media->BlockSize, &Bytes) ||
        Bytes < SFB_STORE_MIN_PARTITION_BYTES) {
      continue;
    }

    /* Addressed relative to the partition, so its end is the media's end. */
    *BlockIo = Candidate;
    *PartitionEnd = Bytes;
    Status = EFI_SUCCESS;
    break;
  }

  FreePool (Handles);

  return Status;
}

/* Resolve once and remember the answer, good or bad. */
STATIC
EFI_STATUS
SfbResolveStore (VOID)
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count = 0;
  UINTN       Index;
  UINT64      PartitionEnd = 0;

  if (mSfbStore.Resolved) {
    return EFI_SUCCESS;
  }
  if (mSfbStore.Failed) {
    return EFI_NOT_FOUND;
  }

  /* Whole disks first: reading the GPT ourselves works even where nothing has
   * published partition metadata. */
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid,
                                    NULL, &Count, &Handles);
  if (!EFI_ERROR (Status) && Handles != NULL) {
    for (Index = 0; Index < Count; Index++) {
      EFI_BLOCK_IO_PROTOCOL  *Disk = NULL;

      if (EFI_ERROR (gBS->HandleProtocol (Handles[Index],
                                          &gEfiBlockIoProtocolGuid,
                                          (VOID **)&Disk)) ||
          Disk->Media == NULL ||
          Disk->Media->LogicalPartition ||
          !Disk->Media->MediaPresent) {
        continue;
      }

      if (!EFI_ERROR (SfbFindEspInGpt (Disk, &PartitionEnd))) {
        mSfbStore.BlockIo = Disk;
        break;
      }
    }

    FreePool (Handles);
  }

  if (mSfbStore.BlockIo == NULL) {
    Status = SfbFindEspByPartitionInfo (&mSfbStore.BlockIo, &PartitionEnd);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "SFB: no EFI System Partition found\n"));
      mSfbStore.Failed = TRUE;
      return EFI_NOT_FOUND;
    }
  }

  if (mSfbStore.BlockIo->Media->ReadOnly) {
    DEBUG ((EFI_D_ERROR, "SFB: ESP media is read only\n"));
    /* Still usable for reads, so this is not a resolution failure. */
  }

  mSfbStore.PartitionEnd = PartitionEnd;
  mSfbStore.Resolved = TRUE;

  DEBUG ((EFI_D_INFO, "SFB: store partition end 0x%lx (block size %u)\n",
          mSfbStore.PartitionEnd,
          (UINT32)mSfbStore.BlockIo->Media->BlockSize));

  return EFI_SUCCESS;
}

/* ---- record access ------------------------------------------------------ */

/*
 * The store is not block aligned in general - a 4 KiB sector device can put a
 * record inside one block - so every access works on the whole run of blocks
 * that covers one named slot, and a write is a read-modify-write of that run.
 *
 * TailDistance is the permanent distance from PartitionEnd to the start of
 * the named record. This deliberately avoids a moving base when new records
 * are added in the reserved scratch area.
 */
STATIC
EFI_STATUS
SfbMapStoreBlocks (IN UINT64      TailDistance,
                   OUT VOID      **Buffer,
                   OUT UINTN      *Pages,
                   OUT EFI_LBA   *Lba,
                   OUT UINTN     *Blocks,
                   OUT UINTN     *Skip)
{
  EFI_STATUS  Status;
  UINTN       BlockSize;
  UINT64      RecordOffset;

  if (Buffer == NULL || Pages == NULL || Lba == NULL ||
      Blocks == NULL || Skip == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = SfbResolveStore ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (TailDistance < SFB_STORE_SLOT_BYTES ||
      TailDistance > mSfbStore.PartitionEnd) {
    return EFI_VOLUME_CORRUPTED;
  }

  RecordOffset = mSfbStore.PartitionEnd - TailDistance;
  if (!SfbValidBlockMedia (mSfbStore.BlockIo->Media)) {
    return EFI_VOLUME_CORRUPTED;
  }
  BlockSize = mSfbStore.BlockIo->Media->BlockSize;

  *Lba = RecordOffset / BlockSize;
  *Skip = (UINTN)(RecordOffset % BlockSize);
  if (*Skip > MAX_UINTN - SFB_STORE_SLOT_BYTES ||
      *Skip + SFB_STORE_SLOT_BYTES > MAX_UINTN - (BlockSize - 1)) {
    return EFI_VOLUME_CORRUPTED;
  }
  *Blocks = (*Skip + SFB_STORE_SLOT_BYTES + BlockSize - 1) / BlockSize;

  return SfbReadBlocks (mSfbStore.BlockIo, *Lba, *Blocks, Buffer, Pages);
}
STATIC
UINT64
SfbLegacyTailDistance (IN UINTN Slot)
{
  return (Slot == SFB_STORE_DEFAULT)
           ? SFB_STORE_DEFAULT_TAIL_DISTANCE
           : SFB_STORE_CUSTOM_TAIL_DISTANCE;
}


STATIC
EFI_STATUS
SfbStoreReadTail (IN UINT64   TailDistance,
                  OUT CHAR8  *Out,
                  IN UINTN    OutBytes)
{
  EFI_STATUS     Status;
  VOID           *Buffer = NULL;
  UINTN          Pages = 0;
  EFI_LBA        Lba = 0;
  UINTN          Blocks = 0;
  UINTN          Skip = 0;
  CONST CHAR8    *Record;
  UINTN          Index;

  if (Out == NULL || OutBytes == 0) {
    return EFI_INVALID_PARAMETER;
  }

  Out[0] = '\0';

  Status = SfbMapStoreBlocks (TailDistance, &Buffer, &Pages, &Lba, &Blocks,
                              &Skip);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Record = (CONST CHAR8 *)Buffer + Skip;

  /* Never-written media is whatever the flash was left as, so nothing beyond
   * the record's own bounds is trusted and the copy is terminated by us. */
  for (Index = 0;
       Index < SFB_STORE_SLOT_BYTES && Index < OutBytes - 1;
       Index++) {
    CHAR8  Ch = Record[Index];

    if (Ch == '\0' || Ch == '\r' || Ch == '\n') {
      break;
    }
    /* Printable 7-bit only: a record is text by definition, and this keeps
     * garbage from reaching the console or the path handling. */
    if (Ch < 0x20 || (UINT8)Ch > 0x7e) {
      Index = 0;
      break;
    }
    Out[Index] = Ch;
  }

  Out[Index] = '\0';

  FreeAlignedPages (Buffer, Pages);

  return EFI_SUCCESS;
}

STATIC EFI_HANDLE  mSfbMigrationVolume;
STATIC BOOLEAN     mSfbMigrationAttempted;

VOID
SfbStoreResetMigration (VOID)
{
  mSfbMigrationVolume = NULL;
  mSfbMigrationAttempted = FALSE;
}

STATIC
EFI_STATUS
SfbStoreMigrateIfNeeded (VOID)
{
  EFI_STATUS  Status;
  CHAR8       Mode[SFB_STORE_SLOT_BYTES];
  CHAR8       Default[SFB_STORE_SLOT_BYTES];
  CHAR8       Custom[SFB_STORE_SLOT_BYTES];
  BOOLEAN     Wrote;

  if (!SfbConfigVolumeBound ()) {
    return EFI_NOT_FOUND;
  }
  if (mSfbMigrationVolume != SfbConfigVolume ()) {
    mSfbMigrationVolume = SfbConfigVolume ();
    mSfbMigrationAttempted = FALSE;
  }
  if (mSfbMigrationAttempted) {
    return EFI_ALREADY_STARTED;
  }
  mSfbMigrationAttempted = TRUE;
  Status = SfbStoreReadTail (SFB_STORE_MODE_TAIL_DISTANCE, Mode,
                              sizeof (Mode));
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = SfbStoreReadTail (SFB_STORE_DEFAULT_TAIL_DISTANCE, Default,
                             sizeof (Default));
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = SfbStoreReadTail (SFB_STORE_CUSTOM_TAIL_DISTANCE, Custom,
                             sizeof (Custom));
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = SfbConfigMigrate (Mode, Default, Custom, &Wrote);
  if (!EFI_ERROR (Status) && Wrote) {
    DEBUG ((EFI_D_INFO, "SFB: MARK store-migrated\n"));
  } else if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK store-write-refused status=%r\n", Status));
  }
  return Status;
}

STATIC
EFI_STATUS
SfbStoreReadPreferred (IN UINTN Slot,
                       OUT CHAR8 *Out,
                       IN UINTN OutBytes)
{
  EFI_STATUS  Status;
  BOOLEAN     FilePresent;
  BOOLEAN     ValuePresent;

  if (!SfbConfigVolumeBound ()) {
    return EFI_NOT_FOUND;
  }
  Status = SfbConfigReadSlot (Slot + 1, Out, OutBytes, &FilePresent,
                              &ValuePresent);
  if (EFI_ERROR (Status)) {
    /* A damaged host-edited file must not make the menu unreachable. */
    Status = SfbStoreReadTail (SfbLegacyTailDistance (Slot), Out, OutBytes);
    if (!EFI_ERROR (Status)) {
      DEBUG ((EFI_D_INFO, "SFB: MARK store-source=legacy-tail\n"));
    }
    return Status;
  }
  if (FilePresent) {
    DEBUG ((EFI_D_INFO, "SFB: MARK store-source=bootconfig\n"));
    if (!ValuePresent) {
      Out[0] = '\0';
    }
    return EFI_SUCCESS;
  }
  Status = SfbStoreMigrateIfNeeded ();
  if (EFI_ERROR (Status) && Status != EFI_ALREADY_STARTED) {
    /* The legacy tail remains the rollback source after a failed migration. */
  }
  Status = SfbStoreReadTail (SfbLegacyTailDistance (Slot), Out, OutBytes);
  if (!EFI_ERROR (Status)) {
    DEBUG ((EFI_D_INFO, "SFB: MARK store-source=%a\n",
            (Out[0] == '\0') ? "default" : "legacy-tail"));
  }
  return Status;
}
EFI_STATUS
SfbStoreRead (IN UINTN Slot, OUT CHAR8 *Out, IN UINTN OutBytes)
{
  EFI_STATUS Status;

  if (Slot >= SFB_STORE_SLOTS || Out == NULL || OutBytes == 0) {
    return EFI_INVALID_PARAMETER;
  }
  Status = SfbStoreReadPreferred (Slot, Out, OutBytes);
  if (Status != EFI_NOT_FOUND) {
    return Status;
  }
  Status = SfbStoreReadTail (SfbLegacyTailDistance (Slot), Out, OutBytes);
  if (!EFI_ERROR (Status)) {
    DEBUG ((EFI_D_INFO, "SFB: MARK store-source=%a\n",
            (Out[0] == '\0') ? "default" : "legacy-tail"));
  }
  return Status;
}

STATIC
EFI_STATUS
SfbStoreWriteTail (IN UINT64      TailDistance,
                   IN CONST CHAR8 *Text)
{
  EFI_STATUS  Status;
  VOID        *Buffer = NULL;
  UINTN       Pages = 0;
  EFI_LBA     Lba = 0;
  UINTN       Blocks = 0;
  UINTN       Skip = 0;
  CHAR8       *Record;
  UINTN       Length;

  if (Text == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Length = AsciiStrLen (Text);
  if (Length >= SFB_STORE_SLOT_BYTES) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Status = SfbMapStoreBlocks (TailDistance, &Buffer, &Pages, &Lba, &Blocks,
                              &Skip);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (mSfbStore.BlockIo->Media->ReadOnly) {
    FreeAlignedPages (Buffer, Pages);
    return EFI_WRITE_PROTECTED;
  }

  /* Only this named slot changes; all other bytes in the covering blocks are
   * put back exactly as they were read. */
  Record = (CHAR8 *)Buffer + Skip;
  ZeroMem (Record, SFB_STORE_SLOT_BYTES);
  CopyMem (Record, Text, Length);

  Status = mSfbStore.BlockIo->WriteBlocks (mSfbStore.BlockIo,
                                           mSfbStore.BlockIo->Media->MediaId,
                                           Lba,
                                           Blocks *
                                             mSfbStore.BlockIo->Media->BlockSize,
                                           Buffer);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: store write failed: %r\n", Status));
  } else {
    /* Flushing matters: the user may power the device off the moment the menu
     * hands over to the image it just recorded. */
    Status = mSfbStore.BlockIo->FlushBlocks (mSfbStore.BlockIo);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "SFB: store flush failed: %r\n", Status));
    }
  }

  FreeAlignedPages (Buffer, Pages);

  return Status;
}

EFI_STATUS
SfbStoreWrite (IN UINTN Slot, IN CONST CHAR8 *Text)
{
  EFI_STATUS Status;

  if (Slot >= SFB_STORE_SLOTS || Text == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (SfbConfigVolumeBound ()) {
    Status = SfbConfigWriteSlot (Slot + 1, Text);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "SFB: MARK store-write-refused status=%r\n", Status));
    }
    return Status;
  }
  return SfbStoreWriteTail (SfbLegacyTailDistance (Slot), Text);
}

STATIC
EFI_STATUS
SfbStoreReadRawTail (IN UINT64 TailDistance,
                     OUT CHAR8 *Record)
{
  EFI_STATUS  Status;
  VOID        *Buffer = NULL;
  UINTN       Pages = 0;
  EFI_LBA     Lba = 0;
  UINTN       Blocks = 0;
  UINTN       Skip = 0;

  if (Record == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = SfbMapStoreBlocks (TailDistance, &Buffer, &Pages, &Lba, &Blocks,
                              &Skip);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  CopyMem (Record, (CONST UINT8 *)Buffer + Skip, SFB_STORE_SLOT_BYTES);
  FreeAlignedPages (Buffer, Pages);
  return EFI_SUCCESS;
}

STATIC
BOOLEAN
SfbModeRecordMatches (IN CONST CHAR8 *Record, IN CONST CHAR8 *Expected)
{
  UINTN  Index;

  if (CompareMem (Record, Expected, 7) != 0) {
    return FALSE;
  }
  for (Index = 7; Index < SFB_STORE_SLOT_BYTES; Index++) {
    if (Record[Index] != '\0') {
      return FALSE;
    }
  }
  return TRUE;
}

EFI_STATUS
SfbStoreReadMode (OUT SFB_BOOT_MODE *Mode, OUT BOOLEAN *Defaulted)
{
  EFI_STATUS  Status;
  CHAR8       Record[SFB_STORE_SLOT_BYTES];
  BOOLEAN     FilePresent;
  BOOLEAN     ValuePresent;

  if (Mode == NULL || Defaulted == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Mode = SfbBootModeAblFakeLocked;
  *Defaulted = TRUE;

  if (SfbConfigVolumeBound ()) {
    Status = SfbConfigReadSlot (SFB_CONFIG_MODE, Record, sizeof (Record),
                                &FilePresent, &ValuePresent);
    if (EFI_ERROR (Status)) {
      /* Keep booting from the immutable legacy source on read failure. */
      FilePresent = FALSE;
    }
    if (FilePresent) {
      DEBUG ((EFI_D_INFO, "SFB: MARK store-source=bootconfig\n"));
      if (!ValuePresent) {
        DEBUG ((EFI_D_INFO, "SFB: MARK store-source=default\n"));
        return EFI_SUCCESS;
      }
      if (Record[0] == '0') {
        *Mode = SfbBootModeHonestUnlocked;
      } else if (Record[0] == '2') {
        *Mode = SfbBootModeKmProfile;
      } else {
        *Mode = SfbBootModeAblFakeLocked;
      }
      *Defaulted = FALSE;
      return EFI_SUCCESS;
    }
    Status = SfbStoreMigrateIfNeeded ();
    if (EFI_ERROR (Status) && Status != EFI_ALREADY_STARTED) {
      /* Keep the immutable tail as the rollback source. */
    }
  }

  Status = SfbStoreReadRawTail (SFB_STORE_MODE_TAIL_DISTANCE, Record);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (SfbModeRecordMatches (Record, "SFBM1|0")) {
    *Mode = SfbBootModeHonestUnlocked;
    *Defaulted = FALSE;
  } else if (SfbModeRecordMatches (Record, "SFBM1|1")) {
    *Mode = SfbBootModeAblFakeLocked;
    *Defaulted = FALSE;
  } else if (SfbModeRecordMatches (Record, "SFBM1|2")) {
    *Mode = SfbBootModeKmProfile;
    *Defaulted = FALSE;
  } else {
    DEBUG ((EFI_D_INFO, "SFB: MARK store-source=default\n"));
    return EFI_SUCCESS;
  }
  DEBUG ((EFI_D_INFO, "SFB: MARK store-source=legacy-tail\n"));
  return EFI_SUCCESS;
}

EFI_STATUS
SfbStoreWriteMode (IN SFB_BOOT_MODE Mode)
{
  CONST CHAR8  *Record;
  CONST CHAR8  *Value;
  EFI_STATUS   Status;

  switch (Mode) {
  case SfbBootModeHonestUnlocked:
    Record = "SFBM1|0";
    Value = "0";
    break;
  case SfbBootModeAblFakeLocked:
    Record = "SFBM1|1";
    Value = "1";
    break;
  case SfbBootModeKmProfile:
    Record = "SFBM1|2";
    Value = "2";
    break;
  default:
    return EFI_INVALID_PARAMETER;
  }

  if (SfbConfigVolumeBound ()) {
    Status = SfbConfigWriteSlot (SFB_CONFIG_MODE, Value);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "SFB: MARK store-write-refused status=%r\n", Status));
    }
    return Status;
  }
  return SfbStoreWriteTail (SFB_STORE_MODE_TAIL_DISTANCE, Record);
}
EFI_STATUS
SfbCommitModeSelection (
  IN OUT SFB_BOOT_MODE *CurrentMode,
  IN SFB_BOOT_MODE      SelectedMode
  )
{
  EFI_STATUS Status;

  if (CurrentMode == NULL || SelectedMode > SfbBootModeKmProfile) {
    return EFI_INVALID_PARAMETER;
  }

  Status = SfbStoreWriteMode (SelectedMode);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *CurrentMode = SelectedMode;
  DEBUG ((EFI_D_INFO, "SFB: MARK mode-selected mode=%u\n",
          (UINT32)SelectedMode));
  return EFI_SUCCESS;
}

