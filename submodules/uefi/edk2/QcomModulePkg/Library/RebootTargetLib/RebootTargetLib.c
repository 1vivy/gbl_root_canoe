/* SPDX-License-Identifier: BSD-3-Clause
 * Qualcomm target conventions: boot-fastboot/boot-recovery in misc, then normal
 * reset; bootloader uses reset reason 2. Shared by BDS and standalone tools. */
#include <Library/RebootTargetLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/BlockIo.h>

#define BCB_COMMAND_BYTES  32
#define BOOT_ONCE_PREFIX   "canoe-once:"
/* Derived from the literal so the length and the text cannot drift apart. */
#define BOOT_ONCE_PREFIX_BYTES  (sizeof (BOOT_ONCE_PREFIX) - 1)
#define BOOT_ONCE_TAG_DELIMITER  '+'

extern EFI_GUID gEfiMiscPartitionGuid;

typedef struct {
  EFI_HANDLE            *Handles;
  EFI_BLOCK_IO_PROTOCOL *Io;
  CHAR8                 *Bytes;
  UINTN                 Pages;
} REBOOT_MISC_IO;

/*
 * The only place a tag literal, its length and the standard target it means are
 * written down: arming, parsing and logging all read this table, so a tag cannot
 * be half-added. The array is sized from the enum, which turns a new tag without
 * its entry into a compile-time complaint instead of a silent gap.
 */
typedef struct {
  CONST CHAR8             *Tag;
  UINTN                    Bytes;
  REBOOT_TARGET            Reboot;
  REBOOT_BOOT_ONCE_TARGET  Target;
} BOOT_ONCE_TAG;

STATIC CONST BOOT_ONCE_TAG mBootOnceTags[RebootBootOnceTargetCount - 1] = {
  { "recovery",  sizeof ("recovery") - 1,  RebootTargetRecovery,
    RebootBootOnceTargetRecovery  },
  { "fastbootd", sizeof ("fastbootd") - 1, RebootTargetFastbootd,
    RebootBootOnceTargetFastbootd },
};

STATIC CONST BOOT_ONCE_TAG *
RebootTargetTagByTarget (IN REBOOT_BOOT_ONCE_TARGET Target)
{
  UINTN Index;

  for (Index = 0; Index < ARRAY_SIZE (mBootOnceTags); Index++) {
    if (mBootOnceTags[Index].Target == Target) {
      return &mBootOnceTags[Index];
    }
  }
  return NULL;
}

/* Exact match for a tag that must be terminated inside RoomBytes. */
STATIC CONST BOOT_ONCE_TAG *
RebootTargetTagByLiteral (IN CONST CHAR8 *Record, IN UINTN RoomBytes)
{
  UINTN Index;

  for (Index = 0; Index < ARRAY_SIZE (mBootOnceTags); Index++) {
    if (RoomBytes < mBootOnceTags[Index].Bytes + 1) {
      continue;
    }
    if (CompareMem (
          Record, mBootOnceTags[Index].Tag,
          mBootOnceTags[Index].Bytes) == 0 &&
        Record[mBootOnceTags[Index].Bytes] == '\0') {
      return &mBootOnceTags[Index];
    }
  }
  return NULL;
}

/* What is left of the command field for the selector once the prefix, the
 * delimiter, the tag and the terminator are accounted for. */
STATIC UINTN
RebootTargetTaggedSelectorMax (IN CONST BOOT_ONCE_TAG *Tag)
{
  return BCB_COMMAND_BYTES - BOOT_ONCE_PREFIX_BYTES - 1 - Tag->Bytes - 1;
}

STATIC VOID
RebootTargetCloseMisc (IN OUT REBOOT_MISC_IO *Misc)
{
  if (Misc->Bytes != NULL) {
    FreeAlignedPages (Misc->Bytes, Misc->Pages);
  }
  if (Misc->Handles != NULL) {
    FreePool (Misc->Handles);
  }
}

STATIC EFI_STATUS
RebootTargetReadMisc (OUT REBOOT_MISC_IO *Misc)
{
  EFI_STATUS Status;
  UINTN Count = 0;
  UINTN Alignment;

  ZeroMem (Misc, sizeof (*Misc));
  Status = gBS->LocateHandleBuffer (
                  ByProtocol, &gEfiMiscPartitionGuid, NULL, &Count,
                  &Misc->Handles);
  if (EFI_ERROR (Status) || Count == 0) {
    return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;
  }
  if (Count != 1) {
    return EFI_NO_MAPPING;
  }
  Status = gBS->HandleProtocol (
                  Misc->Handles[0], &gEfiBlockIoProtocolGuid,
                  (VOID **)&Misc->Io);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (Misc->Io == NULL || Misc->Io->Media == NULL ||
      Misc->Io->ReadBlocks == NULL || Misc->Io->WriteBlocks == NULL ||
      Misc->Io->FlushBlocks == NULL ||
      Misc->Io->Media->BlockSize < BCB_COMMAND_BYTES) {
    return EFI_UNSUPPORTED;
  }
  Misc->Pages = EFI_SIZE_TO_PAGES (Misc->Io->Media->BlockSize);
  Alignment = Misc->Io->Media->IoAlign > EFI_PAGE_SIZE
                ? Misc->Io->Media->IoAlign : EFI_PAGE_SIZE;
  Misc->Bytes = AllocateAlignedPages (Misc->Pages, Alignment);
  if (Misc->Bytes == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  return Misc->Io->ReadBlocks (
                     Misc->Io, Misc->Io->Media->MediaId, 0,
                     Misc->Io->Media->BlockSize, Misc->Bytes);
}

STATIC EFI_STATUS
RebootTargetWriteCommand (IN OUT REBOOT_MISC_IO *Misc,
                          IN CONST CHAR8         *Command,
                          IN UINTN                CommandBytes)
{
  EFI_STATUS Status;

  ZeroMem (Misc->Bytes, BCB_COMMAND_BYTES);
  if (CommandBytes != 0) {
    CopyMem (Misc->Bytes, Command, CommandBytes);
  }
  Status = Misc->Io->WriteBlocks (
                       Misc->Io, Misc->Io->Media->MediaId, 0,
                       Misc->Io->Media->BlockSize, Misc->Bytes);
  if (!EFI_ERROR (Status)) {
    Status = Misc->Io->FlushBlocks (Misc->Io);
  }
  return Status;
}

/* A selector character. '+' is absent on purpose: it delimits the tag. */
STATIC BOOLEAN
RebootTargetSelectorChar (IN CHAR8 Ch)
{
  return (BOOLEAN)((Ch >= 'A' && Ch <= 'Z') || (Ch >= 'a' && Ch <= 'z') ||
                   (Ch >= '0' && Ch <= '9') || Ch == '.' || Ch == '_' ||
                   Ch == ':' || Ch == '-');
}

/* A 1..MaxBytes selector, terminated inside MaxBytes + 1 bytes. */
STATIC BOOLEAN
RebootTargetSelectorValid (
  IN  CONST CHAR8 *Selector,
  IN  UINTN        MaxBytes,
  OUT UINTN       *Length
  )
{
  UINTN Index;

  if (Selector == NULL || Length == NULL) {
    return FALSE;
  }
  for (Index = 0; Index <= MaxBytes; Index++) {
    CHAR8 Ch = Selector[Index];
    if (Ch == '\0') {
      *Length = Index;
      return (BOOLEAN)(Index != 0);
    }
    if (!RebootTargetSelectorChar (Ch)) {
      return FALSE;
    }
  }
  return FALSE;
}

/* The tagged selector ends at the delimiter rather than at a NUL, so it is
 * checked as a slice of the command field. */
STATIC BOOLEAN
RebootTargetSelectorSlice (
  IN CONST CHAR8 *Selector,
  IN UINTN        Bytes,
  IN UINTN        MaxBytes
  )
{
  UINTN Index;

  if (Bytes == 0 || Bytes > MaxBytes) {
    return FALSE;
  }
  for (Index = 0; Index < Bytes; Index++) {
    if (!RebootTargetSelectorChar (Selector[Index])) {
      return FALSE;
    }
  }
  return TRUE;
}

EFI_STATUS
RebootTargetPrepare (REBOOT_TARGET Target, UINT8 *Reason)
{
  EFI_STATUS Status;
  REBOOT_MISC_IO Misc;
  CONST CHAR8 *Command;

  if (Reason == NULL || Target >= RebootTargetCount) {
    return EFI_INVALID_PARAMETER;
  }
  *Reason = Target == RebootTargetBootloader ? 2 : 0;
  if (Target == RebootTargetBootloader) {
    return EFI_SUCCESS;
  }
  Command = Target == RebootTargetFastbootd ? "boot-fastboot" :
            Target == RebootTargetRecovery ? "boot-recovery" : "";
  Status = RebootTargetReadMisc (&Misc);
  if (EFI_ERROR (Status)) {
    RebootTargetCloseMisc (&Misc);
    return Status;
  }
  if (Target == RebootTargetSystem &&
      CompareMem (Misc.Bytes, "boot-fastboot\0", 14) != 0 &&
      CompareMem (Misc.Bytes, "boot-recovery\0", 14) != 0) {
    Status = EFI_SUCCESS;
  } else {
    Status = RebootTargetWriteCommand (&Misc, Command, AsciiStrLen (Command));
  }
  RebootTargetCloseMisc (&Misc);
  return Status;
}

EFI_STATUS
RebootTargetIsFastbootd (OUT BOOLEAN *Detected)
{
  EFI_STATUS Status;
  REBOOT_MISC_IO Misc;

  if (Detected == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Detected = FALSE;
  Status = RebootTargetReadMisc (&Misc);
  if (!EFI_ERROR (Status)) {
    *Detected = (BOOLEAN)(CompareMem (
                            Misc.Bytes, "boot-fastboot\0",
                            sizeof ("boot-fastboot")) == 0);
  }
  RebootTargetCloseMisc (&Misc);
  return Status;
}

EFI_STATUS
RebootTargetBootOnceArm (
  IN CONST CHAR8             *Selector,
  IN REBOOT_BOOT_ONCE_TARGET  Target
  )
{
  EFI_STATUS           Status;
  REBOOT_MISC_IO       Misc;
  CHAR8                Command[BCB_COMMAND_BYTES];
  CONST BOOT_ONCE_TAG *Tag;
  UINTN                SelectorBytes;
  UINTN                CommandBytes;

  Tag = Target == RebootBootOnceTargetNone
          ? NULL : RebootTargetTagByTarget (Target);
  if (Target != RebootBootOnceTargetNone && Tag == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  /* Both refusals happen before misc is touched: an oversized selector or an
   * unknown tag must never leave a partial record behind. */
  if (!RebootTargetSelectorValid (
         Selector,
         Tag == NULL ? REBOOT_BOOT_ONCE_SELECTOR_MAX
                     : RebootTargetTaggedSelectorMax (Tag),
         &SelectorBytes)) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (Command, sizeof (Command));
  CopyMem (Command, BOOT_ONCE_PREFIX, BOOT_ONCE_PREFIX_BYTES);
  CopyMem (Command + BOOT_ONCE_PREFIX_BYTES, Selector, SelectorBytes);
  CommandBytes = BOOT_ONCE_PREFIX_BYTES + SelectorBytes;
  if (Tag != NULL) {
    Command[CommandBytes] = BOOT_ONCE_TAG_DELIMITER;
    CommandBytes++;
    CopyMem (Command + CommandBytes, Tag->Tag, Tag->Bytes);
    CommandBytes += Tag->Bytes;
  }
  Status = RebootTargetReadMisc (&Misc);
  if (!EFI_ERROR (Status)) {
    Status = RebootTargetWriteCommand (&Misc, Command, CommandBytes);
  }
  RebootTargetCloseMisc (&Misc);
  return Status;
}

EFI_STATUS
RebootTargetBootOnceReadAndClear (
  OUT CHAR8                    Selector[REBOOT_BOOT_ONCE_SELECTOR_BYTES],
  OUT REBOOT_BOOT_ONCE_TARGET *Target,
  OUT BOOLEAN                 *Found
  )
{
  EFI_STATUS               Status;
  REBOOT_MISC_IO           Misc;
  CHAR8                    Parsed[REBOOT_BOOT_ONCE_SELECTOR_BYTES];
  CONST CHAR8             *Record;
  CONST BOOT_ONCE_TAG     *Tag;
  UINTN                    FieldBytes = BCB_COMMAND_BYTES - BOOT_ONCE_PREFIX_BYTES;
  UINTN                    Index;
  UINTN                    SelectorBytes = 0;
  BOOLEAN                  Valid;
  REBOOT_BOOT_ONCE_TARGET  ParsedTarget = RebootBootOnceTargetNone;

  if (Selector == NULL || Target == NULL || Found == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Selector[0] = '\0';
  *Target     = RebootBootOnceTargetNone;
  *Found      = FALSE;
  Status      = RebootTargetReadMisc (&Misc);
  if (EFI_ERROR (Status)) {
    RebootTargetCloseMisc (&Misc);
    return Status;
  }
  if (CompareMem (Misc.Bytes, BOOT_ONCE_PREFIX, BOOT_ONCE_PREFIX_BYTES) != 0) {
    RebootTargetCloseMisc (&Misc);
    return EFI_SUCCESS;
  }
  Record = Misc.Bytes + BOOT_ONCE_PREFIX_BYTES;
  /* The selector runs up to the end of the string or to the tag delimiter; a
   * record with neither inside the command field is malformed. */
  for (Index = 0; Index < FieldBytes && Record[Index] != '\0' &&
                  Record[Index] != BOOT_ONCE_TAG_DELIMITER; Index++) {
  }
  Tag = NULL;
  if (Index == FieldBytes) {
    Valid = FALSE;
  } else if (Record[Index] == '\0') {
    Valid = RebootTargetSelectorValid (
              Record, REBOOT_BOOT_ONCE_SELECTOR_MAX, &SelectorBytes);
  } else {
    Tag = RebootTargetTagByLiteral (Record + Index + 1, FieldBytes - Index - 1);
    Valid = (BOOLEAN)(Tag != NULL &&
             RebootTargetSelectorSlice (
               Record, Index, RebootTargetTaggedSelectorMax (Tag)));
    if (Valid) {
      SelectorBytes = Index;
      ParsedTarget  = Tag->Target;
    }
  }
  if (!Valid) {
    Status = RebootTargetWriteCommand (&Misc, NULL, 0);
    RebootTargetCloseMisc (&Misc);
    return EFI_ERROR (Status) ? Status : EFI_COMPROMISED_DATA;
  }
  CopyMem (Parsed, Record, SelectorBytes);
  Parsed[SelectorBytes] = '\0';
  Status = RebootTargetWriteCommand (&Misc, NULL, 0);
  if (!EFI_ERROR (Status)) {
    CopyMem (Selector, Parsed, SelectorBytes + 1);
    *Target = ParsedTarget;
    *Found  = TRUE;
  }
  RebootTargetCloseMisc (&Misc);
  return Status;
}

EFI_STATUS
RebootTargetBootOnceClear (VOID)
{
  EFI_STATUS Status;
  REBOOT_MISC_IO Misc;

  Status = RebootTargetReadMisc (&Misc);
  if (!EFI_ERROR (Status) &&
      CompareMem (Misc.Bytes, BOOT_ONCE_PREFIX, BOOT_ONCE_PREFIX_BYTES) == 0) {
    Status = RebootTargetWriteCommand (&Misc, NULL, 0);
  }
  RebootTargetCloseMisc (&Misc);
  return Status;
}

CONST CHAR8 *
RebootTargetBootOnceTagName (IN REBOOT_BOOT_ONCE_TARGET Target)
{
  CONST BOOT_ONCE_TAG *Tag = RebootTargetTagByTarget (Target);

  return Tag == NULL ? NULL : Tag->Tag;
}

BOOLEAN
RebootTargetBootOnceTagTarget (
  IN  REBOOT_BOOT_ONCE_TARGET  Target,
  OUT REBOOT_TARGET           *Reboot
  )
{
  CONST BOOT_ONCE_TAG *Tag;

  if (Reboot == NULL) {
    return FALSE;
  }
  Tag = RebootTargetTagByTarget (Target);
  if (Tag == NULL) {
    return FALSE;
  }
  *Reboot = Tag->Reboot;
  return TRUE;
}

VOID
RebootTargetReset (UINT8 Reason)
{
  struct { CHAR16 Text[12]; UINT8 Reason; } __attribute__((packed, aligned(2))) Data;
  ZeroMem (&Data, sizeof (Data));
  StrCpyS (Data.Text, 12, L"RESET_PARAM");
  Data.Reason = Reason;
  gRT->ResetSystem (EfiResetCold, Reason == 0 ? EFI_SUCCESS : EFI_INVALID_PARAMETER,
                    sizeof (Data), &Data);
}
