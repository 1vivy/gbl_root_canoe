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
#define BOOT_ONCE_PREFIX_BYTES  11

extern EFI_GUID gEfiMiscPartitionGuid;

typedef struct {
  EFI_HANDLE            *Handles;
  EFI_BLOCK_IO_PROTOCOL *Io;
  CHAR8                 *Bytes;
  UINTN                 Pages;
} REBOOT_MISC_IO;

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

STATIC BOOLEAN
RebootTargetSelectorValid (IN CONST CHAR8 *Selector, OUT UINTN *Length)
{
  UINTN Index;

  if (Selector == NULL || Length == NULL) {
    return FALSE;
  }
  for (Index = 0; Index <= REBOOT_BOOT_ONCE_SELECTOR_MAX; Index++) {
    CHAR8 Ch = Selector[Index];
    if (Ch == '\0') {
      *Length = Index;
      return (BOOLEAN)(Index != 0);
    }
    if (!((Ch >= 'A' && Ch <= 'Z') || (Ch >= 'a' && Ch <= 'z') ||
          (Ch >= '0' && Ch <= '9') || Ch == '.' || Ch == '_' ||
          Ch == ':' || Ch == '-')) {
      return FALSE;
    }
  }
  return FALSE;
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
RebootTargetBootOnceArm (IN CONST CHAR8 *Selector)
{
  EFI_STATUS Status;
  REBOOT_MISC_IO Misc;
  CHAR8 Command[BCB_COMMAND_BYTES];
  UINTN SelectorBytes;

  if (!RebootTargetSelectorValid (Selector, &SelectorBytes)) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (Command, sizeof (Command));
  CopyMem (Command, BOOT_ONCE_PREFIX, BOOT_ONCE_PREFIX_BYTES);
  CopyMem (Command + BOOT_ONCE_PREFIX_BYTES, Selector, SelectorBytes);
  Status = RebootTargetReadMisc (&Misc);
  if (!EFI_ERROR (Status)) {
    Status = RebootTargetWriteCommand (
               &Misc, Command, BOOT_ONCE_PREFIX_BYTES + SelectorBytes);
  }
  RebootTargetCloseMisc (&Misc);
  return Status;
}

EFI_STATUS
RebootTargetBootOnceReadAndClear (
  OUT CHAR8   Selector[REBOOT_BOOT_ONCE_SELECTOR_BYTES],
  OUT BOOLEAN *Found
  )
{
  EFI_STATUS Status;
  REBOOT_MISC_IO Misc;
  CHAR8 Parsed[REBOOT_BOOT_ONCE_SELECTOR_BYTES];
  UINTN SelectorBytes;

  if (Selector == NULL || Found == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Selector[0] = '\0';
  *Found = FALSE;
  Status = RebootTargetReadMisc (&Misc);
  if (EFI_ERROR (Status)) {
    RebootTargetCloseMisc (&Misc);
    return Status;
  }
  if (CompareMem (Misc.Bytes, BOOT_ONCE_PREFIX, BOOT_ONCE_PREFIX_BYTES) != 0) {
    RebootTargetCloseMisc (&Misc);
    return EFI_SUCCESS;
  }
  if (!RebootTargetSelectorValid (
         Misc.Bytes + BOOT_ONCE_PREFIX_BYTES, &SelectorBytes)) {
    Status = RebootTargetWriteCommand (&Misc, NULL, 0);
    RebootTargetCloseMisc (&Misc);
    return EFI_ERROR (Status) ? Status : EFI_COMPROMISED_DATA;
  }
  CopyMem (Parsed, Misc.Bytes + BOOT_ONCE_PREFIX_BYTES, SelectorBytes);
  Parsed[SelectorBytes] = '\0';
  Status = RebootTargetWriteCommand (&Misc, NULL, 0);
  if (!EFI_ERROR (Status)) {
    CopyMem (Selector, Parsed, SelectorBytes + 1);
    *Found = TRUE;
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
