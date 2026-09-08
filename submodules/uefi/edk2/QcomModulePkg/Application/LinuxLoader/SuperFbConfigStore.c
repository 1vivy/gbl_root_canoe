/* SPDX-License-Identifier: BSD-3-Clause */
#include "SuperFbConfigStore.h"
#include "SuperFbMenu.h"
#include <Guid/FileInfo.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>

STATIC EFI_STATUS
SfbReadConfigCandidate (EFI_FILE_PROTOCOL *Root, CONST CHAR16 *Path,
                        CHAR8 *Buffer, UINTN *Size, SFB_CONFIG *Config)
{
  EFI_STATUS Status;
  *Size = 0;
  Status = SfbReadFileBytes (Root, Path, Buffer,
                            SFB_CONFIG_MAX_BYTES + 1, Size);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (*Size > SFB_CONFIG_MAX_BYTES ||
      !SfbConfigParse (Buffer, *Size, Config)) {
    return EFI_COMPROMISED_DATA;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
SfbReadStoredConfig (EFI_FILE_PROTOCOL *Root, CHAR8 *Buffer,
                     UINTN *Size, SFB_CONFIG *Config, BOOLEAN *Previous)
{
  EFI_STATUS Current;
  EFI_STATUS Status;
  if (Root == NULL || Buffer == NULL || Size == NULL || Config == NULL ||
      Previous == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Previous = FALSE;
  Current = SfbReadConfigCandidate (Root, SFB_CONFIG_CURRENT_PATH,
                                   Buffer, Size, Config);
  if (Current != EFI_NOT_FOUND && Current != EFI_COMPROMISED_DATA) {
    return Current;
  }
  Status = SfbReadConfigCandidate (Root, SFB_CONFIG_PREVIOUS_PATH,
                                  Buffer, Size, Config);
  if (Status == EFI_NOT_FOUND) {
    return Current;
  }
  *Previous = (BOOLEAN)!EFI_ERROR (Status);
  return Status;
}

