/*
 * BOOTCONFIG on the writable FAT file window.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbConfig.h"
#include "SuperFbMenu.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>

#define SFB_CONFIG_PATH       L"\\BOOTCONFIG"
#define SFB_CONFIG_TEMP_PATH  L"\\BOOTCONFIG.$$$"
#define SFB_CONFIG_MAX_BYTES  4096
#define SFB_CONFIG_SLOT_BYTES 1024

STATIC EFI_HANDLE mSfbConfigVolume;
STATIC CONST CHAR8 *mSfbConfigKeys[SFB_CONFIG_KEYS] = {
  "mode", "default", "custom"
};

STATIC
BOOLEAN
SfbConfigValidValue (IN UINTN Slot, IN CONST CHAR8 *Value, IN UINTN Length)
{
  UINTN Index;

  if (Value == NULL || Length == 0 || Length >= SFB_CONFIG_SLOT_BYTES) {
    return FALSE;
  }
  if (Slot == SFB_CONFIG_MODE) {
    return (BOOLEAN)(Length == 1 && Value[0] >= '0' && Value[0] <= '2');
  }
  for (Index = 0; Index < Length; Index++) {
    if (Value[Index] < 0x21 || Value[Index] > 0x7e) {
      return FALSE;
    }
  }
  return TRUE;
}

STATIC
INTN
SfbConfigKeySlot (IN CONST CHAR8 *Key, IN UINTN Length)
{
  UINTN Index;

  for (Index = 0; Index < SFB_CONFIG_KEYS; Index++) {
    if (AsciiStrLen (mSfbConfigKeys[Index]) == Length &&
        CompareMem (Key, mSfbConfigKeys[Index], Length) == 0) {
      return (INTN)Index;
    }
  }
  return -1;
}

STATIC
VOID
SfbConfigParse (IN CONST CHAR8 *Text,
                IN UINTN TextBytes,
                OUT CHAR8 Values[SFB_CONFIG_KEYS][SFB_CONFIG_SLOT_BYTES],
                OUT BOOLEAN Present[SFB_CONFIG_KEYS])
{
  UINTN Cursor;

  ZeroMem (Values, SFB_CONFIG_KEYS * SFB_CONFIG_SLOT_BYTES);
  ZeroMem (Present, sizeof (BOOLEAN) * SFB_CONFIG_KEYS);
  Cursor = 0;
  while (Cursor < TextBytes) {
    UINTN LineStart = Cursor;
    UINTN Equals = MAX_UINTN;
    UINTN LineEnd;
    UINTN KeyLength;
    UINTN ValueStart;
    UINTN ValueLength;
    INTN Slot;

    while (Cursor < TextBytes && Text[Cursor] != '\n') {
      if (Text[Cursor] == '=' && Equals == MAX_UINTN) {
        Equals = Cursor;
      }
      Cursor++;
    }
    LineEnd = Cursor;
    if (Cursor < TextBytes) {
      Cursor++;
    }
    if (LineEnd > LineStart && Text[LineEnd - 1] == '\r') {
      LineEnd--;
    }
    if (Equals == MAX_UINTN || Equals <= LineStart || Equals >= LineEnd) {
      continue;
    }
    KeyLength = Equals - LineStart;
    ValueStart = Equals + 1;
    ValueLength = LineEnd - ValueStart;
    Slot = SfbConfigKeySlot (Text + LineStart, KeyLength);
    if (Slot < 0 || !SfbConfigValidValue ((UINTN)Slot, Text + ValueStart,
                                           ValueLength)) {
      continue;
    }
    CopyMem (Values[Slot], Text + ValueStart, ValueLength);
    Values[Slot][ValueLength] = '\0';
    Present[Slot] = TRUE;
  }
}

STATIC
EFI_STATUS
SfbConfigReadFile (OUT CHAR8 *Text, OUT UINTN *Bytes, OUT BOOLEAN *Present)
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Root = NULL;

  *Bytes = 0;
  *Present = FALSE;
  Status = SfbOpenVolumeRoot (mSfbConfigVolume, &Root);
  if (EFI_ERROR (Status) || Root == NULL) {
    return Status;
  }
  Status = SfbReadFileBytes (Root, SFB_CONFIG_PATH, Text,
                             SFB_CONFIG_MAX_BYTES + 1, Bytes);
  Root->Close (Root);
  if (Status == EFI_NOT_FOUND) {
    return EFI_NOT_FOUND;
  }
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (*Bytes > SFB_CONFIG_MAX_BYTES) {
    return EFI_BAD_BUFFER_SIZE;
  }
  *Present = TRUE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
SfbConfigReadAll (OUT CHAR8 Values[SFB_CONFIG_KEYS][SFB_CONFIG_SLOT_BYTES],
                  OUT BOOLEAN Present[SFB_CONFIG_KEYS],
                  OUT BOOLEAN *FilePresent)
{
  EFI_STATUS Status;
  CHAR8      Text[SFB_CONFIG_MAX_BYTES + 1];
  UINTN      Bytes;
  BOOLEAN    Exists;

  Status = SfbConfigReadFile (Text, &Bytes, &Exists);
  if (Status == EFI_NOT_FOUND) {
    ZeroMem (Values, SFB_CONFIG_KEYS * SFB_CONFIG_SLOT_BYTES);
    ZeroMem (Present, sizeof (BOOLEAN) * SFB_CONFIG_KEYS);
    *FilePresent = FALSE;
    return EFI_SUCCESS;
  }
  if (EFI_ERROR (Status)) {
    return Status;
  }
  SfbConfigParse (Text, Bytes, Values, Present);
  *FilePresent = Exists;
  return EFI_SUCCESS;
}

VOID
SfbConfigBindVolume (IN EFI_HANDLE Volume)
{
  mSfbConfigVolume = Volume;
  SfbStoreResetMigration ();
}

VOID
SfbConfigUnbindVolume (VOID)
{
  mSfbConfigVolume = NULL;
  SfbStoreResetMigration ();
}

BOOLEAN
SfbConfigVolumeBound (VOID)
{
  return (BOOLEAN)(mSfbConfigVolume != NULL);
}

EFI_HANDLE
SfbConfigVolume (VOID)
{
  return mSfbConfigVolume;
}

EFI_STATUS
SfbConfigReadSlot (IN UINTN Slot,
                   OUT CHAR8 *Out,
                   IN UINTN OutBytes,
                   OUT BOOLEAN *FilePresent,
                   OUT BOOLEAN *ValuePresent)
{
  EFI_STATUS Status;
  CHAR8      Values[SFB_CONFIG_KEYS][SFB_CONFIG_SLOT_BYTES];
  BOOLEAN    Present[SFB_CONFIG_KEYS];

  if (Slot >= SFB_CONFIG_KEYS || Out == NULL || OutBytes == 0 ||
      FilePresent == NULL || ValuePresent == NULL || mSfbConfigVolume == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *FilePresent = FALSE;
  *ValuePresent = FALSE;
  Out[0] = '\0';
  Status = SfbConfigReadAll (Values, Present, FilePresent);
  if (EFI_ERROR (Status) || !*FilePresent || !Present[Slot]) {
    return Status;
  }
  if (AsciiStrLen (Values[Slot]) >= OutBytes) {
    return EFI_BUFFER_TOO_SMALL;
  }
  CopyMem (Out, Values[Slot], AsciiStrLen (Values[Slot]) + 1);
  *ValuePresent = TRUE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
SfbConfigWriteText (IN CONST CHAR8 *Text, IN UINTN TextBytes)
{
  EFI_STATUS             Status;
  EFI_FILE_PROTOCOL      *Root = NULL;
  EFI_FILE_PROTOCOL      *Temp = NULL;
  EFI_FILE_PROTOCOL      *Old = NULL;
  EFI_FILE_INFO          *Info = NULL;
  EFI_BLOCK_IO_PROTOCOL  *BlockIo = NULL;
  UINTN                  InfoBytes;
  UINTN                  WriteBytes;

  if (gBS != NULL && !EFI_ERROR (gBS->HandleProtocol (
      mSfbConfigVolume, &gEfiBlockIoProtocolGuid, (VOID **)&BlockIo)) &&
      BlockIo != NULL && BlockIo->Media != NULL && BlockIo->Media->ReadOnly) {
    return EFI_WRITE_PROTECTED;
  }

  Status = SfbOpenVolumeRoot (mSfbConfigVolume, &Root);
  if (EFI_ERROR (Status) || Root == NULL) {
    return Status;
  }
  Status = Root->Open (Root, &Temp, (CHAR16 *)SFB_CONFIG_TEMP_PATH,
                       EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                       EFI_FILE_MODE_CREATE, 0);
  if (EFI_ERROR (Status) || Temp == NULL) {
    Root->Close (Root);
    return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
  }
  WriteBytes = TextBytes;
  Status = Temp->Write (Temp, &WriteBytes, (VOID *)Text);
  if (!EFI_ERROR (Status) && WriteBytes != TextBytes) {
    Status = EFI_DEVICE_ERROR;
  }
  if (!EFI_ERROR (Status)) {
    Status = Temp->Flush (Temp);
  }
  if (EFI_ERROR (Status)) {
    Temp->Delete (Temp);
    Root->Close (Root);
    return Status;
  }

  Status = Root->Open (Root, &Old, (CHAR16 *)SFB_CONFIG_PATH,
                       EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
  if (!EFI_ERROR (Status) && Old != NULL) {
    Status = Old->Delete (Old);
    Old = NULL;
  } else if (Status == EFI_NOT_FOUND) {
    Status = EFI_SUCCESS;
  }
  if (EFI_ERROR (Status)) {
    Temp->Delete (Temp);
    Root->Close (Root);
    return Status;
  }

  InfoBytes = SIZE_OF_EFI_FILE_INFO + StrSize (SFB_CONFIG_PATH);
  Info = AllocateZeroPool (InfoBytes);
  if (Info == NULL) {
    Temp->Delete (Temp);
    Root->Close (Root);
    return EFI_OUT_OF_RESOURCES;
  }
  Info->Size = InfoBytes;
  Info->FileSize = TextBytes;
  CopyMem (Info->FileName, SFB_CONFIG_PATH, StrSize (SFB_CONFIG_PATH));
  Status = Temp->SetInfo (Temp, &gEfiFileInfoGuid, InfoBytes, Info);
  FreePool (Info);
  if (EFI_ERROR (Status)) {
    Temp->Delete (Temp);
  } else {
    Status = Temp->Close (Temp);
  }
  Root->Close (Root);
  return Status;
}

STATIC
EFI_STATUS
SfbConfigBuildText (IN CHAR8 Values[SFB_CONFIG_KEYS][SFB_CONFIG_SLOT_BYTES],
                    IN BOOLEAN Present[SFB_CONFIG_KEYS],
                    OUT CHAR8 *Text,
                    OUT UINTN *TextBytes)
{
  UINTN Slot;
  UINTN Cursor = 0;
  UINTN Length;
  UINTN KeyLength;

  for (Slot = 0; Slot < SFB_CONFIG_KEYS; Slot++) {
    if (!Present[Slot]) {
      continue;
    }
    Length = AsciiStrLen (Values[Slot]);
    KeyLength = AsciiStrLen (mSfbConfigKeys[Slot]);
    if (Cursor > SFB_CONFIG_MAX_BYTES - Length - KeyLength - 2) {
      return EFI_BAD_BUFFER_SIZE;
    }
    CopyMem (Text + Cursor, mSfbConfigKeys[Slot], KeyLength);
    Cursor += KeyLength;
    Text[Cursor++] = '=';
    CopyMem (Text + Cursor, Values[Slot], Length);
    Cursor += Length;
    Text[Cursor++] = '\n';
  }
  *TextBytes = Cursor;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
SfbConfigVerify (IN CHAR8 Expected[SFB_CONFIG_KEYS][SFB_CONFIG_SLOT_BYTES],
                 IN BOOLEAN ExpectedPresent[SFB_CONFIG_KEYS])
{
  UINTN       Slot;
  CHAR8       Value[SFB_CONFIG_SLOT_BYTES];
  BOOLEAN     FilePresent;
  BOOLEAN     ValuePresent;
  EFI_STATUS  Status;

  for (Slot = 0; Slot < SFB_CONFIG_KEYS; Slot++) {
    Status = SfbConfigReadSlot (Slot, Value, sizeof (Value), &FilePresent,
                                &ValuePresent);
    if (EFI_ERROR (Status) || !FilePresent ||
        ValuePresent != ExpectedPresent[Slot] ||
        (ValuePresent && AsciiStrCmp (Value, Expected[Slot]) != 0)) {
      return EFI_COMPROMISED_DATA;
    }
  }
  return EFI_SUCCESS;
}

EFI_STATUS
SfbConfigWriteSlot (IN UINTN Slot, IN CONST CHAR8 *Value)
{
  EFI_STATUS Status;
  CHAR8 Values[SFB_CONFIG_KEYS][SFB_CONFIG_SLOT_BYTES];
  BOOLEAN Present[SFB_CONFIG_KEYS];
  BOOLEAN FilePresent;
  CHAR8 Text[SFB_CONFIG_MAX_BYTES];
  UINTN TextBytes;
  EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;

  if (Slot >= SFB_CONFIG_KEYS || Value == NULL ||
      !SfbConfigValidValue (Slot, Value, AsciiStrLen (Value))) {
    return EFI_INVALID_PARAMETER;
  }
  if (mSfbConfigVolume == NULL) {
    return EFI_WRITE_PROTECTED;
  }
  if (gBS != NULL && !EFI_ERROR (gBS->HandleProtocol (
      mSfbConfigVolume, &gEfiBlockIoProtocolGuid, (VOID **)&BlockIo)) &&
      BlockIo != NULL && BlockIo->Media != NULL && BlockIo->Media->ReadOnly) {
    return EFI_WRITE_PROTECTED;
  }
  Status = SfbConfigReadAll (Values, Present, &FilePresent);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  CopyMem (Values[Slot], Value, AsciiStrLen (Value) + 1);
  Present[Slot] = TRUE;
  Status = SfbConfigBuildText (Values, Present, Text, &TextBytes);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = SfbConfigWriteText (Text, TextBytes);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK store-write-refused status=%r\n", Status));
    return Status;
  }
  return SfbConfigVerify (Values, Present);
}

EFI_STATUS
SfbConfigMigrate (IN CONST CHAR8 *Mode,
                  IN CONST CHAR8 *Default,
                  IN CONST CHAR8 *Custom,
                  OUT BOOLEAN *Wrote)
{
  CHAR8 Values[SFB_CONFIG_KEYS][SFB_CONFIG_SLOT_BYTES];
  BOOLEAN Present[SFB_CONFIG_KEYS];
  CHAR8 Text[SFB_CONFIG_MAX_BYTES];
  UINTN TextBytes;
  EFI_STATUS Status;

  if (Wrote == NULL || mSfbConfigVolume == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Wrote = FALSE;
  ZeroMem (Values, sizeof (Values));
  ZeroMem (Present, sizeof (Present));
  if (Mode != NULL && AsciiStrLen (Mode) == 7 &&
      CompareMem (Mode, "SFBM1|", 6) == 0 && Mode[6] >= '0' && Mode[6] <= '2') {
    Values[SFB_CONFIG_MODE][0] = Mode[6];
    Values[SFB_CONFIG_MODE][1] = '\0';
    Present[SFB_CONFIG_MODE] = TRUE;
  }
  if (Default != NULL && SfbConfigValidValue (SFB_CONFIG_DEFAULT, Default,
                                                AsciiStrLen (Default))) {
    CopyMem (Values[SFB_CONFIG_DEFAULT], Default, AsciiStrLen (Default) + 1);
    Present[SFB_CONFIG_DEFAULT] = TRUE;
  }
  if (Custom != NULL && SfbConfigValidValue (SFB_CONFIG_CUSTOM, Custom,
                                               AsciiStrLen (Custom))) {
    CopyMem (Values[SFB_CONFIG_CUSTOM], Custom, AsciiStrLen (Custom) + 1);
    Present[SFB_CONFIG_CUSTOM] = TRUE;
  }
  if (!Present[SFB_CONFIG_MODE] && !Present[SFB_CONFIG_DEFAULT] &&
      !Present[SFB_CONFIG_CUSTOM]) {
    return EFI_SUCCESS;
  }
  Status = SfbConfigBuildText (Values, Present, Text, &TextBytes);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = SfbConfigWriteText (Text, TextBytes);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: MARK store-write-refused status=%r\n", Status));
    return Status;
  }
  Status = SfbConfigVerify (Values, Present);
  if (!EFI_ERROR (Status)) {
    *Wrote = TRUE;
  }
  return Status;
}
