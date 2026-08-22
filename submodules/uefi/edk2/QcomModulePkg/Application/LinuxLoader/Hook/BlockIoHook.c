/* Universal vendor-reserve write suppression.
 *
 * The vendor relock path zeroes the fastboot unlock / DeepTest token stored in
 * a vendor reserve partition. That write is destructive and one-way: once the
 * token block is zeroed the device can no longer be unlocked from fastboot, so
 * a chainloaded ABL must never be allowed to land it. Writes to a protected
 * reserve partition are swallowed and reported as successful, which is what the
 * ABL caller expects and what keeps its state machine walking forward.
 *
 * Universal and fail-soft by construction. The protected set is a GPT-name
 * table, not a device assumption, and a platform carrying none of these
 * partitions arms nothing: absence is an expected outcome, never a launch
 * failure. Only `oplusreserve1` and its legacy `opporeserve1` name are
 * evidence-backed as token carriers, so those are the only entries; a new
 * vendor is one line, and a partition whose semantics are unknown is left
 * alone rather than blanket-blocked.
 *
 * The slot is wrapped only across a managed ABL launch and the swallow is gated
 * on SfbHooksActive(), so superfastboot's own `fastboot flash oplusreserve1`
 * still reaches flash.
 */
#include "HookCommon.h"

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Guid/Gpt.h>
#include <Protocol/BlockIo.h>

#define SFB_RESERVE_MAX_RECORDS  8u
#define SFB_GPT_NAME_CHARS       36u

/* Offsets back from the partition's last block, from static RE of the Oplus
 * LinuxLoader and confirmed against a PLR110 dump: LastBlock-0x3A5 is the
 * DeepTest fastboot-unlock token, LastBlock-0x35C the UnlockRecord. Applied
 * only to a name-matched Oplus reserve partition, so a foreign platform never
 * reaches this arithmetic. */
#define SFB_RESERVE_TOKEN_DELTA         0x3A5ULL
#define SFB_RESERVE_UNLOCKRECORD_DELTA  0x35CULL

/* How long the on-screen token notice is held, in microseconds. */
#define SFB_RESERVE_NOTICE_STALL_US  (3u * 1000u * 1000u)

extern EFI_GUID gEfiPartitionRecordGuid;

STATIC CONST CHAR16 *CONST gSfbReserveNames[] = {
  L"oplusreserve1",
  L"opporeserve1",
};

typedef struct {
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  EFI_BLOCK_WRITE OrigWriteBlocks;
  EFI_LBA LastBlockAtInstall;
  CHAR8 NameAscii[SFB_GPT_NAME_CHARS + 1];
  BOOLEAN TokenNoticePrinted;
} SFB_RESERVE_RECORD;

STATIC SFB_RESERVE_RECORD gSfbReserve[SFB_RESERVE_MAX_RECORDS];
STATIC UINTN gSfbReserveCount = 0;

/* Case-insensitive match against a GPT name field, which is a CHAR16[36] that
 * is not required to be NUL-terminated and is space-padded by some writers. */
STATIC BOOLEAN
SfbGptNameMatches (IN CONST CHAR16 *Stored, IN CONST CHAR16 *Want)
{
  UINTN Index;

  if (Stored == NULL || Want == NULL) return FALSE;
  for (Index = 0; Index < SFB_GPT_NAME_CHARS && Want[Index] != L'\0'; Index++) {
    CHAR16 A = Stored[Index];
    CHAR16 B = Want[Index];

    if (A >= L'A' && A <= L'Z') A = (CHAR16)(A | 0x20);
    if (B >= L'A' && B <= L'Z') B = (CHAR16)(B | 0x20);
    if (A != B) return FALSE;
  }
  return Index >= SFB_GPT_NAME_CHARS ||
         Stored[Index] == L'\0' || Stored[Index] == L' ';
}

STATIC BOOLEAN
SfbIsReserveName (IN CONST CHAR16 *Stored)
{
  UINTN Index;

  for (Index = 0; Index < ARRAY_SIZE (gSfbReserveNames); Index++) {
    if (SfbGptNameMatches (Stored, gSfbReserveNames[Index])) return TRUE;
  }
  return FALSE;
}

STATIC VOID
SfbCopyNameAscii (OUT CHAR8 *Dst, IN CONST CHAR16 *Src)
{
  UINTN Index;

  for (Index = 0; Index < SFB_GPT_NAME_CHARS; Index++) {
    CHAR16 Wide = (Src == NULL) ? L'\0' : Src[Index];

    if (Wide == L'\0') break;
    Dst[Index] = (CHAR8)((Wide <= 0x7f) ? Wide : '?');
  }
  Dst[Index] = '\0';
}

STATIC BOOLEAN
SfbBufferIsAllZero (IN CONST VOID *Buffer, IN UINTN BufferSize)
{
  CONST UINT8 *Bytes = (CONST UINT8 *)Buffer;
  UINTN Index;

  if (Buffer == NULL || BufferSize == 0) return FALSE;
  for (Index = 0; Index < BufferSize; Index++) {
    if (Bytes[Index] != 0) return FALSE;
  }
  return TRUE;
}

/* Classify the write. Only the two blocks that carry unlock authorisation are
 * protected; the reserve partition's routine writers (Phoenix boot accounting,
 * charge/UFS state) are none of our business and are forwarded. Blocking those
 * as well -- which an earlier revision did -- silently froze state the platform
 * expects to advance every boot, for no gain in token preservation.
 *
 * Geometry-based, hence Oplus-only by construction: see the delta definitions.
 * A partition too small for the deltas classifies as routine, so the arithmetic
 * can never wrap into a false positive. */
STATIC CONST CHAR8 *
SfbReserveWriteReason (
  IN CONST SFB_RESERVE_RECORD *Record,
  IN EFI_LBA Lba,
  IN UINTN BufferSize,
  IN CONST VOID *Buffer
  )
{
  if (Record->LastBlockAtInstall < SFB_RESERVE_TOKEN_DELTA) {
    return "reserve-write";
  }
  if (Lba == Record->LastBlockAtInstall - SFB_RESERVE_TOKEN_DELTA) {
    /* An all-zero token block is the relock path erasing unlock authorisation;
     * a non-zero write still overwrites the token we are preserving. */
    return SfbBufferIsAllZero (Buffer, BufferSize) ? "token-zero-write"
                                                  : "token-block-write";
  }
  if (Lba == Record->LastBlockAtInstall - SFB_RESERVE_UNLOCKRECORD_DELTA) {
    return "unlock-record-write";
  }
  return "reserve-write";
}

/* Routine traffic is forwarded; everything else names a protected block. */
STATIC BOOLEAN
SfbReasonIsProtected (IN CONST CHAR8 *Reason)
{
  return AsciiStrCmp (Reason, "reserve-write") != 0;
}

STATIC EFI_STATUS EFIAPI
HookedReserveWriteBlocks (
  IN EFI_BLOCK_IO_PROTOCOL *This,
  IN UINT32 MediaId,
  IN EFI_LBA Lba,
  IN UINTN BufferSize,
  IN VOID *Buffer
  )
{
  UINTN Index;
  CONST CHAR8 *Reason;

  for (Index = 0; Index < gSfbReserveCount; Index++) {
    SFB_RESERVE_RECORD *Record = &gSfbReserve[Index];

    if (Record->BlockIo != This) continue;
    if (Record->OrigWriteBlocks == NULL) return EFI_NOT_READY;
    if (!SfbHooksActive ()) {
      return Record->OrigWriteBlocks (This, MediaId, Lba, BufferSize, Buffer);
    }
    Reason = SfbReserveWriteReason (Record, Lba, BufferSize, Buffer);
    if (!SfbReasonIsProtected (Reason)) {
      DEBUG ((EFI_D_INFO,
              "SFB: MARK reserve-write-pass p=%a lba=%Lu bytes=%u\n",
              Record->NameAscii, (UINT64)Lba, (UINT32)BufferSize));
      return Record->OrigWriteBlocks (This, MediaId, Lba, BufferSize, Buffer);
    }

    /* Deliberately not gated on nesting depth: a firmware callback re-entering
     * this slot beneath an outer write must be suppressed too. */
    DEBUG ((EFI_D_WARN,
            "SFB: MARK reserve-write-swallow reason=%a p=%a lba=%Lu bytes=%u "
            "universal=1\n",
            Reason, Record->NameAscii, (UINT64)Lba, (UINT32)BufferSize));
    /* The token erase is the only event surfaced on screen. DEBUG reaches the
     * UART log alone, so without this the one thing a user needs to see is
     * invisible. Held for three seconds because the boot moves on immediately
     * and an unheld line is gone before it can be read. Once per partition per
     * launch: the relock path retries, and repeating the hold would stack
     * multi-second stalls into the boot. */
    if (AsciiStrCmp (Reason, "token-zero-write") == 0 &&
        !Record->TokenNoticePrinted) {
      Record->TokenNoticePrinted = TRUE;
      Print (L"SFB: blocked unlock-token erase on %a LBA %Lu; token preserved\n",
             Record->NameAscii, (UINT64)Lba);
      if (gBS != NULL && gBS->Stall != NULL) {
        gBS->Stall (SFB_RESERVE_NOTICE_STALL_US);
      }
    }
    return EFI_SUCCESS;
  }

  return EFI_INVALID_PARAMETER;
}

/* Wraps every protected reserve partition found. Partial arming is safe -- each
 * record retains its own original -- so there is no separate preflight phase.
 * EFI_NOT_FOUND means this platform has no such partition. */
EFI_STATUS
SfbInstallReserveBlockIo (VOID)
{
  EFI_STATUS Status;
  EFI_HANDLE *Handles = NULL;
  UINTN HandleCount = 0;
  UINTN Index;
  UINTN Installed = 0;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL,
                                    &HandleCount, &Handles);
  if (EFI_ERROR (Status) || Handles == NULL) return EFI_NOT_FOUND;

  for (Index = 0; Index < HandleCount; Index++) {
    EFI_PARTITION_ENTRY *PartEntry = NULL;
    EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;

    Status = gBS->HandleProtocol (Handles[Index], &gEfiPartitionRecordGuid,
                                  (VOID **)&PartEntry);
    if (EFI_ERROR (Status) || PartEntry == NULL) continue;
    if (!SfbIsReserveName (PartEntry->PartitionName)) continue;

    Status = gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid,
                                  (VOID **)&BlockIo);
    if (EFI_ERROR (Status) || BlockIo == NULL) continue;
    /* Already ours from a previous launch: the retained original stays. */
    if (BlockIo->WriteBlocks == HookedReserveWriteBlocks) {
      Installed++;
      continue;
    }
    if (BlockIo->WriteBlocks == NULL) continue;
    if (gSfbReserveCount >= SFB_RESERVE_MAX_RECORDS) {
      DEBUG ((EFI_D_WARN, "SFB: MARK reserve-table-full count=%u\n",
              (UINT32)gSfbReserveCount));
      break;
    }

    gSfbReserve[gSfbReserveCount].BlockIo = BlockIo;
    gSfbReserve[gSfbReserveCount].OrigWriteBlocks = BlockIo->WriteBlocks;
    gSfbReserve[gSfbReserveCount].LastBlockAtInstall =
      (BlockIo->Media != NULL) ? BlockIo->Media->LastBlock : 0;
    gSfbReserve[gSfbReserveCount].TokenNoticePrinted = FALSE;
    SfbCopyNameAscii (gSfbReserve[gSfbReserveCount].NameAscii,
                      PartEntry->PartitionName);
    BlockIo->WriteBlocks = HookedReserveWriteBlocks;
    DEBUG ((EFI_D_INFO, "SFB: MARK reserve-armed p=%a last-block=%Lu "
            "token-lba=%Lu\n",
            gSfbReserve[gSfbReserveCount].NameAscii,
            (UINT64)gSfbReserve[gSfbReserveCount].LastBlockAtInstall,
            (UINT64)(gSfbReserve[gSfbReserveCount].LastBlockAtInstall -
                     SFB_RESERVE_TOKEN_DELTA)));
    gSfbReserveCount++;
    Installed++;
  }

  gBS->FreePool (Handles);
  return (Installed != 0) ? EFI_SUCCESS : EFI_NOT_FOUND;
}

VOID
SfbRestoreReserveBlockIo (VOID)
{
  UINTN Index;

  for (Index = 0; Index < gSfbReserveCount; Index++) {
    SFB_RESERVE_RECORD *Record = &gSfbReserve[Index];

    if (Record->BlockIo != NULL &&
        Record->BlockIo->WriteBlocks == HookedReserveWriteBlocks) {
      Record->BlockIo->WriteBlocks = Record->OrigWriteBlocks;
    }
    Record->BlockIo = NULL;
    Record->OrigWriteBlocks = NULL;
    Record->LastBlockAtInstall = 0;
    Record->TokenNoticePrinted = FALSE;
    Record->NameAscii[0] = '\0';
  }
  gSfbReserveCount = 0;
}
