/** @file
 *  RebootTools - a standalone UEFI tool launched from the super-fastboot boot
 *  menu. Offers four reboot targets (Fastbootd, Bootloader, Recovery, System).
 *
 *  RebootDevice and the misc-partition BCB write are ported from the r32 tree
 *  (ShutdownServices.c / Recovery.c) but trimmed to the self-contained paths:
 *  the reset is a single gRT->ResetSystem call, and the misc partition is found
 *  by its type GUID via LocateHandleBuffer rather than the full GPT enumerator.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>

#include "AtReboot.h"
#include "AndroidToolsUi.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)  (sizeof (a) / sizeof ((a)[0]))
#endif

/* ---- ported r32 reboot/recovery helpers --------------------------------- */

VOID
AtRebootDevice (
  IN UINT8 Reason
  )
{
  AT_RESET_DATA ResetData;
  EFI_STATUS    Status;

  Status = (Reason == NORMAL_MODE) ? EFI_SUCCESS : EFI_INVALID_PARAMETER;

  StrnCpyS (ResetData.DataBuffer, ARRAY_SIZE (ResetData.DataBuffer),
            (CONST CHAR16 *)AT_RESET_PARAM, ARRAY_SIZE (AT_RESET_PARAM) - 1);
  ResetData.Bdata = Reason;

  gRT->ResetSystem (EfiResetCold, Status, sizeof (ResetData), &ResetData);
}

EFI_STATUS
AtWriteRecoveryMessage (
  IN CHAR8 *Command
  )
{
  EFI_STATUS             Status;
  EFI_HANDLE            *Handles = NULL;
  UINTN                  Count = 0;
  EFI_BLOCK_IO_PROTOCOL *BlkIo = NULL;
  UINT32                 BlkSize;
  UINTN                  Size;
  VOID                  *Buf = NULL;
  struct RecoveryMessage *Msg;

  if (Command == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  /* The platform partition driver installs the partition type GUID as a
   * protocol on each partition handle, so misc is found directly. */
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiMiscPartitionGuid, NULL,
                                    &Count, &Handles);
  if (EFI_ERROR (Status) || Count == 0) {
    DEBUG ((DEBUG_ERROR, "AT: misc partition not found: %r\n", Status));
    return (EFI_ERROR (Status)) ? Status : EFI_NOT_FOUND;
  }

  Status = gBS->HandleProtocol (Handles[0], &gEfiBlockIoProtocolGuid,
                                (VOID **)&BlkIo);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AT: misc BlockIo open failed: %r\n", Status));
    FreePool (Handles);
    return Status;
  }

  BlkSize = (UINT32)BlkIo->Media->BlockSize;
  if (BlkSize == 0) {
    BlkSize = 4096;
  }
  /* Read/write two blocks, mirroring r32's two-page window. */
  Size = (UINTN)BlkSize * 2;

  Buf = AllocateZeroPool (Size);
  if (Buf == NULL) {
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }

  Status = BlkIo->ReadBlocks (BlkIo, BlkIo->Media->MediaId, 0, Size, Buf);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AT: misc read failed: %r\n", Status));
    goto Out;
  }

  /* UFS / non-NAND: the bootloader message lives at offset 0. */
  Msg = (struct RecoveryMessage *)Buf;
  AsciiStrnCpyS (Msg->Command, sizeof (Msg->Command), Command,
                 AsciiStrLen (Command));

  Status = BlkIo->WriteBlocks (BlkIo, BlkIo->Media->MediaId, 0, Size, Buf);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AT: misc write failed: %r\n", Status));
  }

  /* Best effort: flush so the BCB is durable before the reset. */
  if (BlkIo->FlushBlocks != NULL) {
    BlkIo->FlushBlocks (BlkIo);
  }

Out:
  FreePool (Buf);
  FreePool (Handles);
  return Status;
}

/* ---- menu --------------------------------------------------------------- */

EFI_STATUS
EFIAPI
RebootToolsEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  STATIC CONST CHAR16 *Items[] = {
    L"Reboot to Fastbootd",
    L"Reboot to Bootloader",
    L"Reboot to Recovery",
    L"Reboot to System",
    L"Back",
  };
  UINTN      Sel;
  EFI_STATUS Status;

  /*
   * The power press that selected us in the super-fastboot menu is often still
   * held when we start; drain it (after a release delay) so it cannot confirm
   * the first entry the instant the menu appears.
   */
  AtUiEnterMenu (L"Reboot Tools");

  while (TRUE) {
    Status = AtUiRunMenu (L"Reboot Tools", Items, ARRAY_SIZE (Items), &Sel,
                          L"Vol+/- move, power select");
    if (EFI_ERROR (Status)) {
      continue;
    }

    switch (Sel) {
    case 0:  /* Reboot to Fastbootd */
      AtUiShowMessage (L"Rebooting to Fastbootd...");
      Status = AtWriteRecoveryMessage (AT_RECOVERY_BOOT_FASTBOOT);
      if (EFI_ERROR (Status)) {
        AtUiReportStatus (L"Write BCB", Status);
        break;
      }
      AtRebootDevice (NORMAL_MODE);
      break;

    case 1:  /* Reboot to Bootloader */
      AtUiShowMessage (L"Rebooting to Bootloader...");
      AtRebootDevice (FASTBOOT_MODE);
      break;

    case 2:  /* Reboot to Recovery */
      AtUiShowMessage (L"Rebooting to Recovery...");
      Status = AtWriteRecoveryMessage (AT_RECOVERY_BOOT_RECOVERY);
      if (EFI_ERROR (Status)) {
        AtUiReportStatus (L"Write BCB", Status);
        break;
      }
      AtRebootDevice (NORMAL_MODE);
      break;

    case 3:  /* Reboot to System */
      AtUiShowMessage (L"Rebooting to System...");
      AtRebootDevice (NORMAL_MODE);
      break;

    case 4:  /* Back - exit to the boot menu */
      return EFI_SUCCESS;

    default:
      break;
    }
  }

  return EFI_SUCCESS;
}
