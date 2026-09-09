/* SPDX-License-Identifier: BSD-3-Clause */
#include "SuperFbConfigStore.h"
#include "SuperFbMenu.h"
#include <Guid/FileInfo.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>

STATIC EFI_STATUS
SfbDeleteConfigFile (EFI_FILE_PROTOCOL *Root, CONST CHAR16 *Path)
{
  EFI_FILE_PROTOCOL *File = NULL;
  EFI_STATUS Status = Root->Open (Root, &File, (CHAR16 *)Path,
                                   EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
  if (Status == EFI_NOT_FOUND) {
    return EFI_SUCCESS;
  }
  if (EFI_ERROR (Status) || File == NULL) {
    return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
  }
  /* Delete closes the file even on EFI_WARN_DELETE_FAILURE. */
  Status = File->Delete (File);
  return Status == EFI_SUCCESS ? Status : EFI_DEVICE_ERROR;
}

STATIC EFI_STATUS
SfbPublishConfig (EFI_FILE_PROTOCOL *Root, CONST CHAR16 *Destination,
                   CONST CHAR8 *Bytes, UINTN Size)
{
  STATIC UINT32 Sequence;
  EFI_FILE_PROTOCOL *File = NULL;
  EFI_FILE_PROTOCOL *Existing = NULL;
  EFI_FILE_INFO *Info = NULL;
  CHAR16 Temporary[48];
  EFI_STATUS Status = EFI_ALREADY_STARTED;
  UINTN Attempt;
  UINTN Written;
  UINTN InfoSize;

  /* A private BDS mount has no concurrent USB writer. Still never truncate a
   * temporary file left by a previous interrupted boot. */
  for (Attempt = 0; Attempt < 32; Attempt++) {
    UnicodeSPrint (Temporary, sizeof (Temporary), L"\\.canoe-cfg-stage-%08x",
                   ++Sequence);
    Status = Root->Open (Root, &Existing, Temporary, EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR (Status) && Existing != NULL) {
      Existing->Close (Existing);
      Existing = NULL;
      continue;
    }
    if (Status != EFI_NOT_FOUND) {
      return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
    }
    Status = Root->Open (Root, &File, Temporary,
                         EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ |
                         EFI_FILE_MODE_WRITE, 0);
    break;
  }
  if (EFI_ERROR (Status) || File == NULL) {
    return EFI_ERROR (Status) ? Status : EFI_OUT_OF_RESOURCES;
  }
  Written = Size;
  Status = File->Write (File, &Written, (VOID *)Bytes);
  if (!EFI_ERROR (Status) && Written != Size) {
    Status = EFI_VOLUME_FULL;
  }
  if (!EFI_ERROR (Status)) {
    Status = File->Flush (File);
  }
  if (EFI_ERROR (Status)) {
    goto Done;
  }
  InfoSize = SIZE_OF_EFI_FILE_INFO + StrSize (Destination);
  Info = AllocateZeroPool (InfoSize);
  if (Info == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }
  Info->Size = InfoSize;
  Info->FileSize = Size;
  Info->PhysicalSize = Size;
  StrCpyS (Info->FileName, StrLen (Destination) + 1, Destination);
  /* EFI FAT does not replace an existing name through SetInfo. The caller
   * has flushed a valid previous config before deleting the current name. */
  Status = SfbDeleteConfigFile (Root, Destination);
  if (!EFI_ERROR (Status)) {
    Status = File->SetInfo (File, &gEfiFileInfoGuid, InfoSize, Info);
  }
  if (!EFI_ERROR (Status)) {
    Status = File->Flush (File);
  }
Done:
  if (Info != NULL) {
    FreePool (Info);
  }
  if (File != NULL) {
    EFI_STATUS Closed = File->Close (File);
    if (!EFI_ERROR (Status)) {
      Status = Closed;
    }
  }
  if (!EFI_ERROR (Status)) {
    Status = Root->Flush (Root);
  }
  /* A failed rename leaves this owned temporary file; a successful rename
   * leaves no temporary name. Cleanup cannot erase the published generation. */
  if (EFI_ERROR (Status)) {
    (VOID)SfbDeleteConfigFile (Root, Temporary);
  }
  return Status;
}

EFI_STATUS
SfbStoreConfigDefault (EFI_FILE_PROTOCOL *Root,
                        CONST CHAR8 *Target, UINT8 Mode)
{
  CHAR8 *Current = NULL;
  CHAR8 *Next = NULL;
  SFB_CONFIG *Config = NULL;
  UINTN Size;
  UINTN NextSize = SFB_CONFIG_MAX_BYTES;
  BOOLEAN Previous;
  EFI_STATUS Status = EFI_OUT_OF_RESOURCES;

  Current = AllocateZeroPool (SFB_CONFIG_MAX_BYTES + 1);
  Next = AllocateZeroPool (SFB_CONFIG_MAX_BYTES + 1);
  Config = AllocateZeroPool (sizeof (*Config));
  if (Current == NULL || Next == NULL || Config == NULL) {
    goto Done;
  }
  Status = SfbReadStoredConfig (Root, Current, &Size, Config, &Previous);
  if (EFI_ERROR (Status)) {
    goto Done;
  }
  if (!SfbConfigEditDefault (Current, Size, Target, Mode, Next, &NextSize)) {
    Status = EFI_INVALID_PARAMETER;
    goto Done;
  }
  if (!Previous) {
    Status = SfbPublishConfig (Root, SFB_CONFIG_PREVIOUS_PATH, Current, Size);
    if (EFI_ERROR (Status)) {
      goto Done;
    }
  }
  Status = SfbPublishConfig (Root, SFB_CONFIG_CURRENT_PATH, Next, NextSize);
Done:
  if (Current != NULL) { FreePool (Current); }
  if (Next != NULL) { FreePool (Next); }
  if (Config != NULL) { FreePool (Config); }
  return Status;
}
