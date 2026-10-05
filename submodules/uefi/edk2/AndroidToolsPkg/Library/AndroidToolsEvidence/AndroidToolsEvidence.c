/** @file
 *  Crash-safe, append-only evidence files for standalone AndroidTools apps.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Guid/FileInfo.h>
#include <Library/AndroidToolsEvidence.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/PartitionInfo.h>

#include "../../../QcomModulePkg/Application/LinuxLoader/SuperFbGptName.h"

#define AT_EVIDENCE_MAX_TRACKED  32u
#define AT_EVIDENCE_NAME_CHARS   56u
#define AT_EVIDENCE_TOKEN_CHARS  24u
#define AT_EVIDENCE_ROW_CHARS    96u

typedef struct {
  CHAR16 Name[AT_EVIDENCE_NAME_CHARS];
  UINTN  Sequence;
} AT_EVIDENCE_ENTRY;

STATIC AT_EVIDENCE_ENTRY mEntries[AT_EVIDENCE_MAX_TRACKED];

STATIC BOOLEAN
AtEvidenceTokenValid (
  IN CONST CHAR16 *Token,
  IN UINTN         Limit
  )
{
  UINTN Index;

  if (Token == NULL || Token[0] == L'\0') {
    return FALSE;
  }
  for (Index = 0; Token[Index] != L'\0'; Index++) {
    if (Index + 1 >= Limit ||
        !((Token[Index] >= L'a' && Token[Index] <= L'z') ||
          (Token[Index] >= L'A' && Token[Index] <= L'Z') ||
          (Token[Index] >= L'0' && Token[Index] <= L'9') ||
          Token[Index] == L'-' || Token[Index] == L'_')) {
      return FALSE;
    }
  }
  return TRUE;
}

STATIC VOID
AtEvidenceCopy (
  OUT CHAR16       *Destination,
  IN  UINTN         DestinationChars,
  IN  CONST CHAR16 *Source
  )
{
  UINTN Index;

  for (Index = 0; Index + 1 < DestinationChars && Source[Index] != L'\0';
       Index++) {
    Destination[Index] = Source[Index];
  }
  Destination[Index] = L'\0';
}

STATIC CONST CHAR16 *
AtEvidencePartitionName (
  IN EFI_HANDLE Handle
  )
{
  EFI_PARTITION_ENTRY         *Record;
  EFI_PARTITION_INFO_PROTOCOL *Info;

  Record = NULL;
  if (!EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiPartitionRecordGuid,
                                       (VOID **)&Record)) && Record != NULL) {
    return Record->PartitionName;
  }
  Info = NULL;
  if (!EFI_ERROR (gBS->HandleProtocol (Handle,
                                       &gEfiPartitionInfoProtocolGuid,
                                       (VOID **)&Info)) &&
      Info != NULL && Info->Type == PARTITION_TYPE_GPT) {
    return Info->Info.Gpt.PartitionName;
  }
  return NULL;
}

VOID
AtEvidenceStartFatStack (
  VOID
  )
{
  STATIC BOOLEAN Started = FALSE;
  EFI_HANDLE    *Handles;
  UINTN          Count;
  UINTN          Index;
  EFI_STATUS     Status;

  if (Started) {
    return;
  }
  Started = TRUE;
  Handles = NULL;
  Count = 0;
  Status = gBS->LocateHandleBuffer (AllHandles, NULL, NULL, &Count, &Handles);
  if (EFI_ERROR (Status) || Handles == NULL) {
    return;
  }
  for (Index = 0; Index < Count; Index++) {
    gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
  }
  FreePool (Handles);
}

STATIC EFI_STATUS
AtEvidenceOpenLogfsRoot (
  OUT EFI_FILE_PROTOCOL **Root
  )
{
  EFI_STATUS                       Status;
  EFI_HANDLE                      *Handles;
  UINTN                            Count;
  UINTN                            Index;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  CONST CHAR16                    *Name;

  if (Root == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Root = NULL;
  Handles = NULL;
  Count = 0;
  Status = gBS->LocateHandleBuffer (ByProtocol,
                                    &gEfiSimpleFileSystemProtocolGuid,
                                    NULL, &Count, &Handles);
  if (EFI_ERROR (Status) || Handles == NULL) {
    return EFI_NOT_FOUND;
  }
  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < Count; Index++) {
    Name = AtEvidencePartitionName (Handles[Index]);
    if (!SfbGptNameMatchesInline (Name, L"logfs")) {
      continue;
    }
    FileSystem = NULL;
    Status = gBS->HandleProtocol (Handles[Index],
                                  &gEfiSimpleFileSystemProtocolGuid,
                                  (VOID **)&FileSystem);
    if (EFI_ERROR (Status) || FileSystem == NULL ||
        FileSystem->OpenVolume == NULL) {
      Status = EFI_NOT_FOUND;
      continue;
    }
    Status = FileSystem->OpenVolume (FileSystem, Root);
    if (!EFI_ERROR (Status) && *Root != NULL) {
      break;
    }
    *Root = NULL;
  }
  FreePool (Handles);
  return Status;
}

STATIC EFI_STATUS
AtEvidenceEnsureDirectory (
  IN EFI_FILE_PROTOCOL *Root
  )
{
  EFI_FILE_PROTOCOL *Directory;
  EFI_STATUS         Status;

  Directory = NULL;
  Status = Root->Open (Root, &Directory, AT_EVIDENCE_DIR,
                       EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                       0);
  if (Status == EFI_NOT_FOUND) {
    Status = Root->Open (Root, &Directory, AT_EVIDENCE_DIR,
                         EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                         EFI_FILE_MODE_CREATE,
                         EFI_FILE_DIRECTORY);
  }
  if (!EFI_ERROR (Status) && Directory != NULL) {
    Status = Directory->Close (Directory);
  }
  return Status;
}

STATIC BOOLEAN
AtEvidenceSequenceOf (
  IN  CONST CHAR16 *Name,
  IN  CONST CHAR16 *Stem,
  OUT UINTN        *Sequence
  )
{
  UINTN Index;
  UINTN StemLength;
  UINTN Start;
  UINTN Value;

  if (Name == NULL || Stem == NULL || Sequence == NULL) {
    return FALSE;
  }
  for (StemLength = 0; Stem[StemLength] != L'\0'; StemLength++) {
  }
  if (StrnCmp (Name, Stem, StemLength) != 0 || Name[StemLength] != L'-') {
    return FALSE;
  }
  Start = MAX_UINTN;
  for (Index = StemLength + 1; Name[Index] != L'\0'; Index++) {
    if (Name[Index] == L'-') {
      Start = Index + 1;
    }
  }
  if (Start == MAX_UINTN || Start <= StemLength + 2 ||
      Name[Start] < L'0' || Name[Start] > L'9') {
    return FALSE;
  }
  Value = 0;
  for (Index = Start; Name[Index] >= L'0' && Name[Index] <= L'9'; Index++) {
    Value = Value * 10 + (UINTN)(Name[Index] - L'0');
    if (Value > 1000000u) {
      return FALSE;
    }
  }
  if (StrCmp (&Name[Index], L".txt") != 0) {
    return FALSE;
  }
  *Sequence = Value;
  return TRUE;
}

STATIC VOID
AtEvidenceScanDirectory (
  IN  EFI_FILE_PROTOCOL *Root,
  IN  CONST CHAR16      *Stem,
  OUT UINTN             *Count
  )
{
  EFI_FILE_PROTOCOL *Directory;
  EFI_FILE_INFO     *Info;
  UINT8              Buffer[SIZE_OF_EFI_FILE_INFO +
                            AT_EVIDENCE_NAME_CHARS * sizeof (CHAR16)];
  EFI_STATUS         Status;
  UINTN              Size;
  UINTN              Sequence;

  *Count = 0;
  Directory = NULL;
  if (EFI_ERROR (Root->Open (Root, &Directory, AT_EVIDENCE_DIR,
                             EFI_FILE_MODE_READ, 0)) ||
      Directory == NULL) {
    return;
  }
  for (;;) {
    Size = sizeof (Buffer);
    Status = Directory->Read (Directory, &Size, Buffer);
    if (EFI_ERROR (Status) || Size < SIZE_OF_EFI_FILE_INFO) {
      break;
    }
    Info = (EFI_FILE_INFO *)Buffer;
    if (Info->FileName[0] == L'.' ||
        !AtEvidenceSequenceOf (Info->FileName, Stem, &Sequence)) {
      continue;
    }
    if (*Count < AT_EVIDENCE_MAX_TRACKED) {
      AtEvidenceCopy (mEntries[*Count].Name, AT_EVIDENCE_NAME_CHARS,
                      Info->FileName);
      mEntries[*Count].Sequence = Sequence;
      *Count = *Count + 1;
    }
  }
  Directory->Close (Directory);
}

STATIC UINTN
AtEvidenceNextSequence (
  IN EFI_FILE_PROTOCOL *Root,
  IN CONST CHAR16      *Stem
  )
{
  UINTN Count;
  UINTN Highest;
  UINTN Index;

  AtEvidenceScanDirectory (Root, Stem, &Count);
  Highest = 0;
  for (Index = 0; Index < Count; Index++) {
    if (mEntries[Index].Sequence > Highest) {
      Highest = mEntries[Index].Sequence;
    }
  }
  return Highest + 1;
}

STATIC BOOLEAN
AtEvidencePickOldest (
  IN  UINTN         Count,
  IN  CONST CHAR16 *Except,
  OUT UINTN        *Pick
  )
{
  UINTN   Best;
  BOOLEAN Have;
  UINTN   Index;

  Best = 0;
  Have = FALSE;
  for (Index = 0; Index < Count; Index++) {
    if (Except != NULL && StrCmp (mEntries[Index].Name, Except) == 0) {
      continue;
    }
    if (!Have || mEntries[Index].Sequence < mEntries[Best].Sequence) {
      Best = Index;
      Have = TRUE;
    }
  }
  if (Have) {
    *Pick = Best;
  }
  return Have;
}

STATIC VOID
AtEvidencePrune (
  IN EFI_FILE_PROTOCOL *Root,
  IN CONST CHAR16      *Stem,
  IN CONST CHAR16      *KeepPath,
  IN UINTN              Keep
  )
{
  EFI_FILE_PROTOCOL *File;
  CHAR16             Path[AT_EVIDENCE_PATH_CHARS];
  UINTN              Count;
  UINTN              Index;
  UINTN              Pick;

  AtEvidenceScanDirectory (Root, Stem, &Count);
  while (Count > Keep && AtEvidencePickOldest (Count, KeepPath, &Pick)) {
    UnicodeSPrint (Path, sizeof (Path), L"%s\\%s", AT_EVIDENCE_DIR,
                   mEntries[Pick].Name);
    File = NULL;
    if (!EFI_ERROR (Root->Open (Root, &File, Path,
                                EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                                0)) && File != NULL) {
      File->Delete (File);
    }
    for (Index = Pick + 1; Index < Count; Index++) {
      mEntries[Index - 1] = mEntries[Index];
    }
    Count--;
  }
}

EFI_STATUS
AtEvidenceOpen (
  IN  CONST CHAR16 *Stem,
  IN  CONST CHAR16 *Tag,
  IN  UINTN         Keep,
  OUT AT_EVIDENCE  *Evidence
  )
{
  EFI_FILE_PROTOCOL *Probe;
  EFI_FILE_PROTOCOL *Root;
  EFI_STATUS         Status;
  EFI_STATUS         ProbeStatus;
  UINTN              Attempt;
  UINTN              Next;

  if (Evidence == NULL || Keep == 0 ||
      !AtEvidenceTokenValid (Stem, AT_EVIDENCE_STEM_CHARS) ||
      !AtEvidenceTokenValid (Tag, AT_EVIDENCE_TOKEN_CHARS)) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (Evidence, sizeof (*Evidence));
  AtEvidenceStartFatStack ();
  Root = NULL;
  Status = AtEvidenceOpenLogfsRoot (&Root);
  if (EFI_ERROR (Status) || Root == NULL) {
    return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;
  }
  Status = AtEvidenceEnsureDirectory (Root);
  if (EFI_ERROR (Status)) {
    Root->Close (Root);
    return Status;
  }
  Next = AtEvidenceNextSequence (Root, Stem);
  Status = EFI_ACCESS_DENIED;
  for (Attempt = 0; Attempt < 8; Attempt++) {
    UnicodeSPrint (Evidence->Path, sizeof (Evidence->Path),
                   L"%s\\%s-%s-%u.txt", AT_EVIDENCE_DIR, Stem, Tag,
                   (UINT32)(Next + Attempt));
    Probe = NULL;
    ProbeStatus = Root->Open (Root, &Probe, Evidence->Path,
                              EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR (ProbeStatus)) {
      if (Probe == NULL) {
        Status = EFI_DEVICE_ERROR;
        break;
      }
      Probe->Close (Probe);
      continue;
    }
    if (ProbeStatus != EFI_NOT_FOUND) {
      Status = ProbeStatus;
      break;
    }
    Status = Root->Open (Root, &Evidence->File, Evidence->Path,
                         EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                         EFI_FILE_MODE_CREATE, 0);
    if (!EFI_ERROR (Status) && Evidence->File != NULL) {
      AtEvidenceCopy (Evidence->Stem, AT_EVIDENCE_STEM_CHARS, Stem);
      Evidence->Keep = Keep;
      Evidence->Open = TRUE;
      Root->Close (Root);
      return EFI_SUCCESS;
    }
    Evidence->File = NULL;
    break;
  }
  Root->Close (Root);
  Evidence->Path[0] = L'\0';
  return Status;
}

STATIC EFI_STATUS
AtEvidenceWriteBytes (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST VOID        *Buffer,
  IN UINTN              BufferSize
  )
{
  EFI_STATUS Status;
  UINTN      Written;

  if (File == NULL || File->Write == NULL || Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Written = BufferSize;
  Status = File->Write (File, &Written, (VOID *)Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  return (Written == BufferSize) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS
AtEvidenceWriteAscii (
  IN OUT AT_EVIDENCE *Evidence,
  IN     CONST CHAR8 *Text
  )
{
  UINTN Length;

  if (Evidence == NULL || !Evidence->Open || Evidence->File == NULL ||
      Text == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  for (Length = 0; Text[Length] != '\0'; Length++) {
  }
  return AtEvidenceWriteBytes (Evidence->File, Text, Length);
}

EFI_STATUS
AtEvidencePrint (
  IN OUT AT_EVIDENCE *Evidence,
  IN     CONST CHAR16 *Format,
  ...
  )
{
  CHAR16     Row[AT_EVIDENCE_ROW_CHARS];
  CHAR8      Ascii[AT_EVIDENCE_ROW_CHARS + 2];
  VA_LIST    Args;
  UINTN      Index;
  UINTN      Length;
  EFI_STATUS Status;

  if (Evidence == NULL || !Evidence->Open || Evidence->File == NULL ||
      Format == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  VA_START (Args, Format);
  Length = UnicodeVSPrint (Row, sizeof (Row), Format, Args);
  VA_END (Args);
  if (Length == 0 || Length >= AT_EVIDENCE_ROW_CHARS - 1) {
    return EFI_BAD_BUFFER_SIZE;
  }
  for (Index = 0; Index < Length; Index++) {
    Ascii[Index] = (Row[Index] <= 0x7f) ? (CHAR8)Row[Index] : '?';
  }
  Ascii[Index++] = '\r';
  Ascii[Index++] = '\n';
  Status = AtEvidenceWriteBytes (Evidence->File, Ascii, Index);
  if (!EFI_ERROR (Status)) {
    Evidence->Rows++;
  }
  return Status;
}

EFI_STATUS
AtEvidenceFlush (
  IN OUT AT_EVIDENCE *Evidence
  )
{
  if (Evidence == NULL || !Evidence->Open || Evidence->File == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (Evidence->File->Flush == NULL) {
    return EFI_UNSUPPORTED;
  }
  return Evidence->File->Flush (Evidence->File);
}

EFI_STATUS
AtEvidenceClose (
  IN OUT AT_EVIDENCE *Evidence
  )
{
  EFI_FILE_PROTOCOL *Root;
  EFI_STATUS         CloseStatus;
  EFI_STATUS         Status;
  CONST CHAR16       *KeepName;
  UINTN               Index;

  if (Evidence == NULL || !Evidence->Open || Evidence->File == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Status = AtEvidenceFlush (Evidence);
  CloseStatus = Evidence->File->Close (Evidence->File);
  Evidence->File = NULL;
  Evidence->Open = FALSE;
  if (!EFI_ERROR (Status) && EFI_ERROR (CloseStatus)) {
    Status = CloseStatus;
  }
  KeepName = Evidence->Path;
  for (Index = 0; Evidence->Path[Index] != L'\0'; Index++) {
    if (Evidence->Path[Index] == L'\\') {
      KeepName = &Evidence->Path[Index + 1];
    }
  }
  Root = NULL;
  if (!EFI_ERROR (Status) &&
      !EFI_ERROR (AtEvidenceOpenLogfsRoot (&Root)) && Root != NULL) {
    AtEvidencePrune (Root, Evidence->Stem, KeepName, Evidence->Keep);
    Root->Close (Root);
  }
  return Status;
}
