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

extern EFI_GUID gEfiMiscPartitionGuid;

EFI_STATUS RebootTargetPrepare (REBOOT_TARGET Target, UINT8 *Reason)
{
  EFI_STATUS Status;
  EFI_HANDLE *Handles = NULL;
  EFI_BLOCK_IO_PROTOCOL *Io = NULL;
  UINTN Count = 0, Pages = 0, Alignment;
  CHAR8 *Bytes = NULL;
  CONST CHAR8 *Command;
  if (Reason == NULL || Target >= RebootTargetCount) { return EFI_INVALID_PARAMETER; }
  *Reason = Target == RebootTargetBootloader ? 2 : 0;
  if (Target == RebootTargetBootloader) { return EFI_SUCCESS; }
  Command = Target == RebootTargetFastbootd ? "boot-fastboot" :
            Target == RebootTargetRecovery ? "boot-recovery" : "";
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiMiscPartitionGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status) || Count == 0) { return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND; }
  if (Count != 1) { Status = EFI_NO_MAPPING; goto Done; }
  Status = gBS->HandleProtocol (Handles[0], &gEfiBlockIoProtocolGuid, (VOID **)&Io);
  if (EFI_ERROR (Status)) { goto Done; }
  if (Io == NULL || Io->Media == NULL || Io->ReadBlocks == NULL ||
      Io->WriteBlocks == NULL || Io->FlushBlocks == NULL || Io->Media->BlockSize < 32) {
    Status = EFI_UNSUPPORTED; goto Done;
  }
  Pages = EFI_SIZE_TO_PAGES (Io->Media->BlockSize);
  Alignment = Io->Media->IoAlign > EFI_PAGE_SIZE ? Io->Media->IoAlign : EFI_PAGE_SIZE;
  Bytes = AllocateAlignedPages (Pages, Alignment);
  if (Bytes == NULL) { Status = EFI_OUT_OF_RESOURCES; goto Done; }
  Status = Io->ReadBlocks (Io, Io->Media->MediaId, 0, Io->Media->BlockSize, Bytes);
  if (EFI_ERROR (Status)) { goto Done; }
  if (Target == RebootTargetSystem &&
      CompareMem (Bytes, "boot-fastboot\0", 14) != 0 &&
      CompareMem (Bytes, "boot-recovery\0", 14) != 0) {
    Status = EFI_SUCCESS; goto Done;
  }
  ZeroMem (Bytes, 32);
  CopyMem (Bytes, Command, AsciiStrLen (Command));
  Status = Io->WriteBlocks (Io, Io->Media->MediaId, 0, Io->Media->BlockSize, Bytes);
  if (!EFI_ERROR (Status)) { Status = Io->FlushBlocks (Io); }
Done:
  if (Bytes != NULL) { FreeAlignedPages (Bytes, Pages); }
  if (Handles != NULL) { FreePool (Handles); }
  return Status;
}
VOID RebootTargetReset (UINT8 Reason)
{
  struct { CHAR16 Text[12]; UINT8 Reason; } __attribute__((packed, aligned(2))) Data;
  ZeroMem (&Data, sizeof (Data));
  StrCpyS (Data.Text, 12, L"RESET_PARAM");
  Data.Reason = Reason;
  gRT->ResetSystem (EfiResetCold, Reason == 0 ? EFI_SUCCESS : EFI_INVALID_PARAMETER,
                    sizeof (Data), &Data);
}
