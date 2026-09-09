/* Android handoff evidence belongs to the canonical FAT boot root, not logfs.
 * SPDX-License-Identifier: BSD-3-Clause */
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include "SuperFbContainer.h"
#include "SuperFbLastBoot.h"

STATIC CONST CHAR16 mLastBootPath[] = L"\\last-boot";

STATIC EFI_STATUS
SfbRecordSync (EFI_FILE_PROTOCOL *Root)
{
  EFI_STATUS Status;
  if (Root == NULL || Root->Flush == NULL) return EFI_UNSUPPORTED;
  Status = Root->Flush (Root);
  if (EFI_ERROR (Status)) return Status;
  return SfbContainerFlush ();
}

/* Read one byte past the fixed record so trailing bytes cannot be accepted. */
STATIC EFI_STATUS
SfbRecordRead (EFI_FILE_PROTOCOL *Root, UINT8 *Bytes, UINTN *Size)
{
  EFI_FILE_PROTOCOL *File = NULL;
  EFI_STATUS Status, Closed;
  if (Root == NULL || Root->Open == NULL) return EFI_UNSUPPORTED;
  Status = Root->Open (Root, &File, (CHAR16 *)mLastBootPath, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) return Status;
  if (File == NULL) return EFI_DEVICE_ERROR;
  if (File->Read == NULL || File->Close == NULL) {
    if (File->Close != NULL) File->Close (File);
    return EFI_UNSUPPORTED;
  }
  *Size = SFB_LAST_BOOT_BYTES + 1;
  Status = File->Read (File, Size, Bytes);
  Closed = File->Close (File);
  return EFI_ERROR (Status) ? Status : Closed;
}

/* First invalidate the magic, then remove the name. Even a failed deletion
 * cannot leave a valid previous record if the zero-header flush completed.
 * The final reopened read is authoritative; an I/O failure is never absence. */
EFI_STATUS
SfbLastBootClear (VOID)
{
  EFI_FILE_PROTOCOL *Root = NULL, *File = NULL;
  EFI_STATUS Status, Closed;
  UINT8 Bytes[SFB_LAST_BOOT_BYTES + 1], Invalid[4] = { 0 };
  UINTN Size;
  Status = SfbContainerOpenRoot (&Root);
  if (EFI_ERROR (Status)) return Status;
  if (Root == NULL || Root->Open == NULL || Root->Close == NULL) {
    if (Root != NULL && Root->Close != NULL) Root->Close (Root);
    return EFI_UNSUPPORTED;
  }
  Status = Root->Open (Root, &File, (CHAR16 *)mLastBootPath,
                       EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
  if (!EFI_ERROR (Status) && File != NULL) {
    if (File->Write != NULL && File->Flush != NULL) {
      Size = sizeof (Invalid);
      Status = File->Write (File, &Size, Invalid);
      if (!EFI_ERROR (Status) && Size == sizeof (Invalid)) (VOID)File->Flush (File);
    }
    if (File->Delete != NULL) (VOID)File->Delete (File); /* closes even on warning */
    else if (File->Close != NULL) (VOID)File->Close (File);
    File = NULL;
  } else if (File != NULL && File->Close != NULL) {
    File->Close (File);
    File = NULL;
  }
  Status = SfbRecordSync (Root);
  if (!EFI_ERROR (Status)) {
    Status = SfbRecordRead (Root, Bytes, &Size);
    if (Status == EFI_NOT_FOUND ||
        (!EFI_ERROR (Status) && (!SfbLastBootValid (Bytes, Size) || Bytes[5] != SFB_LAST_BOOT_HANDOFF)))
      Status = EFI_SUCCESS;
    else if (!EFI_ERROR (Status)) Status = EFI_ACCESS_DENIED;
  }
  Closed = Root->Close (Root);
  return EFI_ERROR (Status) ? Status : Closed;
}

EFI_STATUS
SfbLastBootWrite (IN CONST UINT8 *Bytes)
{
  EFI_FILE_PROTOCOL *Root = NULL, *File = NULL;
  EFI_STATUS Status, Closed, Cleanup;
  UINT8 Readback[SFB_LAST_BOOT_BYTES + 1];
  UINTN Size;
  if (!SfbLastBootValid (Bytes, SFB_LAST_BOOT_BYTES) || Bytes[5] != SFB_LAST_BOOT_HANDOFF)
    return EFI_INVALID_PARAMETER;
  Status = SfbContainerOpenRoot (&Root);
  if (EFI_ERROR (Status)) return Status;
  if (Root == NULL || Root->Open == NULL || Root->Close == NULL) {
    if (Root != NULL && Root->Close != NULL) Root->Close (Root);
    return EFI_UNSUPPORTED;
  }
  /* Clear may have left a verified-invalid file after deletion failed. Never
   * overwrite it without knowing its length: this attempt stays unavailable. */
  Status = Root->Open (Root, &File, (CHAR16 *)mLastBootPath, EFI_FILE_MODE_READ, 0);
  if (Status != EFI_NOT_FOUND) {
    if (File != NULL && File->Close != NULL) File->Close (File);
    File = NULL;
    Status = EFI_ERROR (Status) ? Status : EFI_ALREADY_STARTED;
    goto Done;
  }
  Status = Root->Open (Root, &File, (CHAR16 *)mLastBootPath,
                       EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
  if (EFI_ERROR (Status) || File == NULL) {
    Status = EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
    goto Done;
  }
  if (File->Write == NULL || File->Flush == NULL || File->Close == NULL) {
    Status = EFI_UNSUPPORTED;
    goto Done;
  }
  Size = SFB_LAST_BOOT_BYTES;
  Status = File->Write (File, &Size, (VOID *)Bytes);
  if (!EFI_ERROR (Status) && Size != SFB_LAST_BOOT_BYTES) Status = EFI_DEVICE_ERROR;
  if (!EFI_ERROR (Status)) Status = File->Flush (File);
Done:
  if (File != NULL && File->Close != NULL) {
    Closed = File->Close (File);
    if (!EFI_ERROR (Status)) Status = Closed;
  }
  if (!EFI_ERROR (Status)) Status = SfbRecordSync (Root);
  if (!EFI_ERROR (Status)) {
    Status = SfbRecordRead (Root, Readback, &Size);
    if (!EFI_ERROR (Status) && (Size != SFB_LAST_BOOT_BYTES ||
        CompareMem (Readback, Bytes, SFB_LAST_BOOT_BYTES) != 0)) Status = EFI_DEVICE_ERROR;
  }
  Closed = Root->Close (Root);
  if (!EFI_ERROR (Status)) Status = Closed;
  if (!EFI_ERROR (Status)) return EFI_SUCCESS;
  /* No boot is blocked just because evidence could not be saved. It is safe
   * to proceed without evidence only after checking that no plausible handoff
   * remains. If cleanup cannot prove that, the caller retains its menu escape. */
  Cleanup = SfbLastBootClear ();
  DEBUG ((EFI_D_WARN, "SFB: MARK last-boot write=%r cleanup=%r\n", Status, Cleanup));
  return !EFI_ERROR (Cleanup) || Cleanup == EFI_NOT_FOUND ? EFI_NOT_FOUND : Cleanup;
}
