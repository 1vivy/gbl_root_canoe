/** @file
 *  FdLoader - copy a raw firmware volume to a physical address and jump.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/LoadedImage.h>

#include <AtRawBoot.h>

#define FD_LOADER_OPTIONS_CHARS  512
#define FD_LOADER_TOKEN_COUNT    4

STATIC
BOOLEAN
FdIsWhitespace (
  IN CHAR16 Character
  )
{
  return (BOOLEAN)((Character == L' ') || (Character == L'\t'));
}

STATIC
BOOLEAN
FdIsHexDigit (
  IN CHAR16 Character
  )
{
  return (BOOLEAN)(((Character >= L'0') && (Character <= L'9')) ||
                   ((Character >= L'a') && (Character <= L'f')) ||
                   ((Character >= L'A') && (Character <= L'F')));
}

STATIC
BOOLEAN
FdParseHex64 (
  IN  CONST CHAR16  *Token,
  OUT UINT64        *Value
  )
{
  CONST CHAR16 *Digits;
  CHAR16       *End;
  RETURN_STATUS ParseStatus;

  if ((Token == NULL) || (Value == NULL) || (*Token == L'\0')) {
    return FALSE;
  }

  Digits = Token;
  if ((Digits[0] == L'0') && ((Digits[1] == L'x') ||
                             (Digits[1] == L'X'))) {
    Digits += 2;
  }
  if (*Digits == L'\0') {
    return FALSE;
  }

  while (*Digits != L'\0') {
    if (!FdIsHexDigit (*Digits)) {
      return FALSE;
    }
    Digits++;
  }

  ParseStatus = StrHexToUint64S (Token, &End, Value);
  if (RETURN_ERROR (ParseStatus) || (*End != L'\0')) {
    return FALSE;
  }

  /* Use the public convenience parser after the bounded validation above. */
  *Value = StrHexToUint64 (Token);
  return TRUE;
}

STATIC
BOOLEAN
FdParseArguments (
  IN OUT CHAR16                  *Options,
  OUT    CONST CHAR16           **FdPath,
  OUT    EFI_PHYSICAL_ADDRESS   *Base,
  OUT    UINTN                  *Size,
  OUT    BOOLEAN                *Force
  )
{
  CONST CHAR16 *Tokens[FD_LOADER_TOKEN_COUNT];
  UINTN        TokenCount;
  BOOLEAN      TooMany;
  CHAR16       *Cursor;
  UINT64       BaseValue;
  UINT64       SizeValue;

  TokenCount = 0;
  TooMany = FALSE;
  Cursor = Options;
  while (*Cursor != L'\0') {
    while (FdIsWhitespace (*Cursor)) {
      Cursor++;
    }
    if (*Cursor == L'\0') {
      break;
    }

    if (TokenCount < FD_LOADER_TOKEN_COUNT) {
      Tokens[TokenCount] = Cursor;
      TokenCount++;
    } else {
      TooMany = TRUE;
    }

    while ((*Cursor != L'\0') && !FdIsWhitespace (*Cursor)) {
      Cursor++;
    }
    if (*Cursor != L'\0') {
      *Cursor = L'\0';
      Cursor++;
    }
  }

  if (TooMany || (TokenCount < 3) ||
      ((TokenCount == 4) && (StrCmp (Tokens[3], L"force") != 0))) {
    return FALSE;
  }

  if (!FdParseHex64 (Tokens[1], &BaseValue) ||
      !FdParseHex64 (Tokens[2], &SizeValue) ||
      (SizeValue > (UINT64)MAX_UINTN)) {
    return FALSE;
  }

  *FdPath = Tokens[0];
  *Base = (EFI_PHYSICAL_ADDRESS)BaseValue;
  *Size = (UINTN)SizeValue;
  *Force = (BOOLEAN)(TokenCount == 4);
  return TRUE;
}

STATIC
VOID
FdPrintUsage (
  VOID
  )
{
  Print (L"Usage: FdLoader.efi <fd-path> <base-hex> <size-hex> [force]\r\n");
}

EFI_STATUS
EFIAPI
FdLoaderEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS                 Status;
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
  CHAR16                    Options[FD_LOADER_OPTIONS_CHARS];
  UINTN                     OptionChars;
  CONST CHAR16              *FdPath;
  EFI_PHYSICAL_ADDRESS      Base;
  UINTN                     Size;
  BOOLEAN                   Force;
  VOID                      *Fd;
  UINTN                     FdSize;

  LoadedImage = NULL;
  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid,
                                (VOID **)&LoadedImage);
  if (EFI_ERROR (Status) || (LoadedImage == NULL)) {
    Print (L"FdLoader: loaded image protocol unavailable (%r)\r\n", Status);
    return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;
  }

  OptionChars = LoadedImage->LoadOptionsSize / sizeof (CHAR16);
  if (OptionChars >= FD_LOADER_OPTIONS_CHARS) {
    OptionChars = FD_LOADER_OPTIONS_CHARS - 1;
  }
  SetMem (Options, sizeof (Options), 0);
  if ((OptionChars != 0) && (LoadedImage->LoadOptions != NULL)) {
    CopyMem (Options, LoadedImage->LoadOptions, OptionChars * sizeof (CHAR16));
  }
  Options[OptionChars] = L'\0';

  if ((LoadedImage->LoadOptions == NULL) ||
      !FdParseArguments (Options, &FdPath, &Base, &Size, &Force)) {
    FdPrintUsage ();
    return EFI_INVALID_PARAMETER;
  }

  Fd = NULL;
  FdSize = 0;
  Status = AtRawReadFile (FdPath, &Fd, &FdSize);
  if (EFI_ERROR (Status)) {
    Print (L"FdLoader: read %s failed (%r)\r\n", FdPath, Status);
    return Status;
  }

  if (FdSize == 0) {
    Print (L"FdLoader: %s is empty\r\n", FdPath);
    if (Fd != NULL) {
      FreePool (Fd);
    }
    return EFI_INVALID_PARAMETER;
  }
  if (FdSize > Size) {
    Print (L"FdLoader: %s is too large (%Lu bytes; window %Lu)\r\n",
           FdPath, (UINT64)FdSize, (UINT64)Size);
    FreePool (Fd);
    return EFI_BAD_BUFFER_SIZE;
  }

  Status = AtRawReserve (Base, Size);
  if (EFI_ERROR (Status)) {
    Print (L"FdLoader: reserve 0x%Lx (%Lu bytes) failed (%r)\r\n",
           Base, (UINT64)Size, Status);
    if (!Force) {
      FreePool (Fd);
      return Status;
    }

    /* The target window is a deliberate hole in the payload firmware's own
     * layout and is very likely free conventional memory; after
     * ExitBootServices the allocator's opinion is moot. */
    Print (L"FdLoader: proceeding without reservation\r\n");
  }

  /* Copy while UEFI still has caches enabled; AtRawJump flushes this range. */
  CopyMem ((VOID *)(UINTN)Base, Fd, FdSize);
  FreePool (Fd);

  Print (L"FdLoader: jumping to 0x%Lx (%Lu-byte image)\r\n",
         Base, (UINT64)FdSize);
  /* The payload firmware establishes no incoming register contract, so Arg0 is 0. */
  AtRawJump (Base, 0, &Base, &Size, 1);
  CpuDeadLoop ();
  return EFI_DEVICE_ERROR;
}
