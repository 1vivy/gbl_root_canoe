/* SPDX-License-Identifier: BSD-3-Clause */
#include "SuperFbBcbCommand.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

#define BCB_OEM_PREFIX  "bcb-command"

STATIC EFI_STATUS
CopyToken (
  IN  CONST CHAR8 *Text,
  IN  UINTN        Bytes,
  OUT CHAR8        Token[REBOOT_TARGET_COMMAND_BYTES]
  )
{
  UINTN Index;

  if (Text == NULL || Token == NULL || Bytes == 0 ||
      Bytes >= REBOOT_TARGET_COMMAND_BYTES) {
    return EFI_INVALID_PARAMETER;
  }
  for (Index = 0; Index < Bytes; Index++) {
    if (Text[Index] < 0x21 || Text[Index] > 0x7e) {
      return EFI_INVALID_PARAMETER;
    }
  }
  ZeroMem (Token, REBOOT_TARGET_COMMAND_BYTES);
  CopyMem (Token, Text, Bytes);
  return EFI_SUCCESS;
}

EFI_STATUS
SfbBcbCommandParse (
  IN  CONST CHAR8             *Argument,
  OUT SFB_BCB_COMMAND_REQUEST *Request
  )
{
  CONST CHAR8 *Operands;
  CONST CHAR8 *Separator;
  UINTN        FirstBytes;
  UINTN        SecondBytes;
  UINTN        PrefixBytes = sizeof (BCB_OEM_PREFIX) - 1;
  EFI_STATUS   Status;

  if (Request == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (Request, sizeof (*Request));
  if (Argument == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (AsciiStrnCmp (Argument, BCB_OEM_PREFIX, PrefixBytes) != 0 ||
      (Argument[PrefixBytes] != '\0' && Argument[PrefixBytes] != ' ')) {
    return EFI_SUCCESS;
  }
  if (Argument[PrefixBytes] == '\0') {
    return EFI_INVALID_PARAMETER;
  }

  Operands = Argument + PrefixBytes + 1;
  if (AsciiStrCmp (Operands, "get") == 0) {
    Request->Action = SfbBcbCommandGet;
    return EFI_SUCCESS;
  }

  if (AsciiStrnCmp (Operands, "set ", sizeof ("set ") - 1) == 0) {
    Request->Action = SfbBcbCommandSet;
    Operands += sizeof ("set ") - 1;
    return CopyToken (
             Operands, AsciiStrLen (Operands), Request->Replacement);
  }

  if (AsciiStrnCmp (Operands, "clear ", sizeof ("clear ") - 1) == 0) {
    Request->Action = SfbBcbCommandClear;
    Operands += sizeof ("clear ") - 1;
    return CopyToken (Operands, AsciiStrLen (Operands), Request->Expected);
  }

  if (AsciiStrnCmp (Operands, "replace ", sizeof ("replace ") - 1) == 0) {
    Request->Action = SfbBcbCommandReplace;
    Operands += sizeof ("replace ") - 1;
    Separator = Operands;
    while (*Separator != '\0' && *Separator != ' ') {
      Separator++;
    }
    if (*Separator != ' ' || Separator[1] == '\0') {
      return EFI_INVALID_PARAMETER;
    }
    FirstBytes = (UINTN)(Separator - Operands);
    SecondBytes = AsciiStrLen (Separator + 1);
    Status = CopyToken (Operands, FirstBytes, Request->Expected);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    return CopyToken (
             Separator + 1, SecondBytes, Request->Replacement);
  }

  return EFI_INVALID_PARAMETER;
}
