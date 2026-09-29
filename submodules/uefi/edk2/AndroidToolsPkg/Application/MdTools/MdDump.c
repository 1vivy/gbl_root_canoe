/** @file
 *  Durable evidence for the MdTools escalation ladder.
 *
 *  One file per rung attempt: `\canoe\md-<tag>-<seq>.txt`, where <tag> names
 *  the pathway and rung (p2r1) and <seq> is one above the highest sequence
 *  already present. A new attempt therefore cannot overwrite or delete an
 *  earlier one, which matters because the earlier file is the only thing that
 *  survives a crash: the whole design is that a rung writes what it is about
 *  to do, flushes it, and only then does it.
 *
 *  Each file is self-contained - full report, the rung's intent record, and
 *  the outcome when the rung returns - so the last file written before a crash
 *  stands alone. Older attempts are pruned only after a later attempt has been
 *  flushed and closed, oldest first, down to MD_EVIDENCE_KEEP files (logfs is a
 *  small fixed partition).
 *
 *  Report text is ASCII for the host shell that reads it.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Guid/FileInfo.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/SimpleFileSystem.h>

#include "MdTools.h"

#define MD_EVIDENCE_MAX_TRACKED 32u
#define MD_EVIDENCE_NAME_CHARS  40u

typedef struct {
  CHAR16 Name[MD_EVIDENCE_NAME_CHARS];
  UINTN  Seq;
} MD_EVIDENCE_ENTRY;

STATIC MD_EVIDENCE_ENTRY mEvidenceEntries[MD_EVIDENCE_MAX_TRACKED];

EFI_STATUS
MdWriteBytes (
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
  if (Status != EFI_SUCCESS) {
    return Status;
  }
  return (Written == BufferSize) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS
MdWriteAscii (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST CHAR8       *Text
  )
{
  UINTN Length;

  if (Text == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Length = 0;
  while (Text[Length] != '\0') {
    Length++;
  }
  return MdWriteBytes (File, Text, Length);
}

EFI_STATUS
MdWriteUnicodeLine (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST CHAR16      *Text
  )
{
  CHAR8 Buffer[AT_ROW_CHARS + 2];
  UINTN Index;

  if (Text == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  for (Index = 0; Index < AT_ROW_CHARS && Text[Index] != L'\0'; Index++) {
    Buffer[Index] = (Text[Index] <= 0x7f) ? (CHAR8)Text[Index] : '?';
  }
  /* A row that fills the buffer exactly cannot be terminated, and silently
     dropping its tail would misreport a measurement. */
  if (Index == AT_ROW_CHARS) {
    return EFI_BAD_BUFFER_SIZE;
  }
  Buffer[Index++] = '\r';
  Buffer[Index++] = '\n';
  return MdWriteBytes (File, Buffer, Index);
}

CONST CHAR16 *
MdFieldFourcc (
  IN UINT32 Value
  )
{
  /* One definition for both the map report and the intent records, so a word
     cannot decode to two different names in two places. */
  switch (Value) {
  case MD_SS_TOC_MAGIC_VALUE:      return L"(TOC)";
  case MD_SS_AOP_TOC_MAGIC_VALUE:  return L"(AOP)";
  case MD_SS_ENABLED_VALUE:        return L"(ENBL)";
  case MD_SS_DISABLED_VALUE:       return L"(DSBL)";
  case MD_SS_ENCR_DONE_VALUE:      return L"(DONE)";
  case MD_SS_ENCR_REQ_VALUE:       return L"(YES)";
  case MD_SS_ENCR_NOTREQ_VALUE:    return L"(NR)";
  case MD_SS_ENCR_START_VALUE:     return L"(STRT)";
  default:                         return L"(----)";
  }
}

STATIC
VOID
MdEvidenceCopyName (
  OUT CHAR16       *Dest,
  IN  UINTN         DestChars,
  IN  CONST CHAR16 *Source
  )
{
  UINTN Index;

  for (Index = 0; Index + 1 < DestChars && Source[Index] != L'\0'; Index++) {
    Dest[Index] = Source[Index];
  }
  Dest[Index] = L'\0';
}

/**
  Read the attempt counter out of `md-<tag>-<seq>.txt`. Only the trailing
  digits after the last '-' count, so a tag may itself contain '-' or digits.
**/
STATIC
BOOLEAN
MdEvidenceSeqOf (
  IN  CONST CHAR16 *Name,
  OUT UINTN        *Seq
  )
{
  UINTN Index;
  UINTN Start;
  UINTN Value;

  if (Name == NULL || Seq == NULL || StrnCmp (Name, L"md-", 3) != 0) {
    return FALSE;
  }
  Start = MAX_UINTN;
  for (Index = 0; Name[Index] != L'\0'; Index++) {
    if (Name[Index] == L'-') {
      Start = Index + 1;
    }
  }
  if (Start == MAX_UINTN || Name[Start] < L'0' || Name[Start] > L'9') {
    return FALSE;
  }
  Value = 0;
  for (Index = Start; Name[Index] >= L'0' && Name[Index] <= L'9'; Index++) {
    Value = Value * 10 + (UINTN)(Name[Index] - L'0');
    if (Value > 1000000u) {
      return FALSE;
    }
  }
  if (Name[Index] != L'.') {
    return FALSE;
  }
  *Seq = Value;
  return TRUE;
}

/** Collect this tool's evidence files, with their counters. Never fails hard. **/
STATIC
VOID
MdEvidenceScanDir (
  IN  EFI_FILE_PROTOCOL *Root,
  OUT UINTN             *Count
  )
{
  EFI_FILE_PROTOCOL *Dir;
  EFI_FILE_INFO     *Info;
  UINT8             Buffer[SIZE_OF_EFI_FILE_INFO +
                          MD_EVIDENCE_NAME_CHARS * sizeof (CHAR16)];
  EFI_STATUS        Status;
  UINTN             Size;
  UINTN             Seq;

  *Count = 0;
  Dir = NULL;
  if (EFI_ERROR (Root->Open (Root, &Dir, MD_EVIDENCE_DIR,
                             EFI_FILE_MODE_READ, 0)) || Dir == NULL) {
    return;
  }
  for (;;) {
    Size = sizeof (Buffer);
    Status = Dir->Read (Dir, &Size, Buffer);
    if (EFI_ERROR (Status) || Size < SIZE_OF_EFI_FILE_INFO) {
      break;
    }
    Info = (EFI_FILE_INFO *)Buffer;
    if (Info->FileName[0] == L'.' ||
        !MdEvidenceSeqOf (Info->FileName, &Seq)) {
      continue;
    }
    if (*Count < MD_EVIDENCE_MAX_TRACKED) {
      MdEvidenceCopyName (mEvidenceEntries[*Count].Name,
                          MD_EVIDENCE_NAME_CHARS, Info->FileName);
      mEvidenceEntries[*Count].Seq = Seq;
      *Count = *Count + 1;
    }
  }
  Dir->Close (Dir);
}

/** First counter that is free, so a new attempt never reuses a name. **/
STATIC
UINTN
MdEvidenceNextSeq (
  IN EFI_FILE_PROTOCOL *Root
  )
{
  UINTN Count;
  UINTN Index;
  UINTN Highest;

  MdEvidenceScanDir (Root, &Count);
  Highest = 0;
  for (Index = 0; Index < Count; Index++) {
    if (mEvidenceEntries[Index].Seq > Highest) {
      Highest = mEvidenceEntries[Index].Seq;
    }
  }
  return Highest + 1;
}

/** Index of the oldest collected file other than Except, if any. **/
STATIC
BOOLEAN
MdEvidencePickOldest (
  IN  UINTN        Count,
  IN  CONST CHAR16 *Except,
  OUT UINTN        *Pick
  )
{
  UINTN   Index;
  UINTN   Best;
  BOOLEAN Have;

  Have = FALSE;
  Best = 0;
  for (Index = 0; Index < Count; Index++) {
    if (Except != NULL && StrCmp (mEvidenceEntries[Index].Name, Except) == 0) {
      continue;
    }
    if (!Have || mEvidenceEntries[Index].Seq < mEvidenceEntries[Best].Seq) {
      Best = Index;
      Have = TRUE;
    }
  }
  if (Have) {
    *Pick = Best;
  }
  return Have;
}

/**
  Delete the oldest attempts beyond MD_EVIDENCE_KEEP. Called only after the
  newest file has been flushed and closed, and it never deletes KeepPath, so
  the record of the run that just happened cannot be removed by its own prune.
**/
STATIC
VOID
MdEvidencePrune (
  IN EFI_FILE_PROTOCOL *Root,
  IN CONST CHAR16      *KeepPath
  )
{
  EFI_FILE_PROTOCOL *File;
  CHAR16             Path[MD_EVIDENCE_PATH_CHARS];
  UINTN              Count;
  UINTN              Pick;
  UINTN              Index;

  MdEvidenceScanDir (Root, &Count);
  while (Count > MD_EVIDENCE_KEEP &&
         MdEvidencePickOldest (Count, KeepPath, &Pick)) {
    UnicodeSPrint (Path, sizeof (Path), L"%s\\%s", MD_EVIDENCE_DIR,
                   mEvidenceEntries[Pick].Name);
    File = NULL;
    if (!EFI_ERROR (Root->Open (Root, &File, Path,
                                EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                                0)) && File != NULL) {
      File->Delete (File);
    }
    /* Drop the entry either way so the loop always makes progress. */
    for (Index = Pick + 1; Index < Count; Index++) {
      mEvidenceEntries[Index - 1] = mEvidenceEntries[Index];
    }
    Count--;
  }
}

EFI_STATUS
MdEvidenceOpen (
  IN  CONST CHAR16 *Tag,
  OUT MD_EVIDENCE  *Evidence
  )
{
  EFI_FILE_PROTOCOL *Root;
  EFI_FILE_PROTOCOL *Probe;
  EFI_STATUS         Status;
  UINTN              Next;
  UINTN              Attempt;

  if (Evidence == NULL || Tag == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (Evidence, sizeof (*Evidence));
  Root = NULL;
  Status = MdOpenLogfsRoot (&Root);
  if (EFI_ERROR (Status) || Root == NULL) {
    return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;
  }
  Next = MdEvidenceNextSeq (Root);
  Status = EFI_ACCESS_DENIED;
  for (Attempt = 0; Attempt < 8; Attempt++) {
    UnicodeSPrint (Evidence->Path, sizeof (Evidence->Path),
                   L"%s\\md-%s-%u.txt", MD_EVIDENCE_DIR, Tag,
                   (UINT32)(Next + Attempt));
    Probe = NULL;
    if (!EFI_ERROR (Root->Open (Root, &Probe, Evidence->Path,
                                EFI_FILE_MODE_READ, 0)) && Probe != NULL) {
      /* The name exists: it is an earlier attempt's record. Skip it. */
      Probe->Close (Probe);
      continue;
    }
    Status = Root->Open (Root, &Evidence->File, Evidence->Path,
                         EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                         EFI_FILE_MODE_CREATE, 0);
    if (!EFI_ERROR (Status) && Evidence->File != NULL) {
      Evidence->Open = TRUE;
      Evidence->Rows = 0;
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

EFI_STATUS
MdEvidencePrint (
  IN OUT MD_EVIDENCE *Evidence,
  IN     CONST CHAR16 *Format,
  ...
  )
{
  AT_ROW     Row;
  VA_LIST    Args;
  UINTN      Length;
  EFI_STATUS Status;

  if (Evidence == NULL || !Evidence->Open || Evidence->File == NULL ||
      Format == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  VA_START (Args, Format);
  Length = UnicodeVSPrint (Row.Text, sizeof (Row.Text), Format, Args);
  VA_END (Args);
  /* Refuse rather than write a shortened row: the row would still look like a
     measurement while missing its tail. */
  if (Length == 0 || Length >= AT_ROW_CHARS - 1) {
    return EFI_BAD_BUFFER_SIZE;
  }
  Status = MdWriteUnicodeLine (Evidence->File, Row.Text);
  if (!EFI_ERROR (Status)) {
    Evidence->Rows++;
  }
  return Status;
}

STATIC
EFI_STATUS
MdEvidenceWriteSection (
  IN OUT MD_EVIDENCE        *Evidence,
  IN CONST AT_REPORT_SOURCE *Source
  )
{
  AT_REPORT  Report;
  EFI_STATUS Status;
  UINTN      Index;

  ZeroMem (&Report, sizeof (Report));
  Status = MdWriteAscii (Evidence->File, "\r\n");
  if (!EFI_ERROR (Status)) {
    Status = MdEvidencePrint (Evidence, L"[%s]", Source->Title);
  }
  if (!EFI_ERROR (Status)) {
    Status = Source->Builder (&Report);
  }
  if (EFI_ERROR (Status)) {
    AtReportFree (&Report);
    return Status;
  }
  for (Index = 0; Index < Report.Count && !EFI_ERROR (Status); Index++) {
    Status = MdEvidencePrint (Evidence, L"%s", Report.Rows[Index].Text);
  }
  if (!EFI_ERROR (Status) && Report.Truncated) {
    Status = MdWriteAscii (Evidence->File, "<truncated>\r\n");
  }
  AtReportFree (&Report);
  return Status;
}

EFI_STATUS
MdEvidenceWriteReports (
  IN OUT MD_EVIDENCE *Evidence
  )
{
  CONST AT_REPORT_SOURCE *Sources;
  EFI_STATUS             Status;
  UINTN                  Count;
  UINTN                  Index;

  if (Evidence == NULL || !Evidence->Open) {
    return EFI_INVALID_PARAMETER;
  }
  Sources = MdReportSources (&Count);
  for (Index = 0; Index < Count; Index++) {
    Status = MdEvidenceWriteSection (Evidence, &Sources[Index]);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }
  return EFI_SUCCESS;
}

EFI_STATUS
MdEvidenceFlush (
  IN OUT MD_EVIDENCE *Evidence
  )
{
  if (Evidence == NULL || !Evidence->Open || Evidence->File == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  /* A driver without Flush cannot promise the bytes left its buffer, and a
     record that might still be in RAM is not evidence. */
  if (Evidence->File->Flush == NULL) {
    return EFI_UNSUPPORTED;
  }
  return Evidence->File->Flush (Evidence->File);
}

EFI_STATUS
MdEvidenceClose (
  IN OUT MD_EVIDENCE *Evidence
  )
{
  EFI_FILE_PROTOCOL *Root;
  EFI_STATUS         Status;
  EFI_STATUS         CloseStatus;

  if (Evidence == NULL || !Evidence->Open || Evidence->File == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Status = MdEvidenceFlush (Evidence);
  CloseStatus = Evidence->File->Close (Evidence->File);
  Evidence->File = NULL;
  Evidence->Open = FALSE;
  if (Status == EFI_SUCCESS && CloseStatus != EFI_SUCCESS) {
    Status = CloseStatus;
  }
  Root = NULL;
  if (Status == EFI_SUCCESS && !EFI_ERROR (MdOpenLogfsRoot (&Root)) &&
      Root != NULL) {
    MdEvidencePrune (Root, Evidence->Path);
    Root->Close (Root);
  }
  return Status;
}
