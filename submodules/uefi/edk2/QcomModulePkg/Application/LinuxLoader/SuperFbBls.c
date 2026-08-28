/*
 * Boot Loader Specification Type #1 entry parser. See SuperFbBls.h.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbBls.h"

/* Keeps the translation unit legal when the feature is compiled out. */
const char *gSfbBlsModuleTag = "SuperFbBls";

static SFB_BOOLEAN
SfbBlsIsSpace (char Character)
{
  return (SFB_BOOLEAN)(Character == ' ' || Character == '\t');
}

static SFB_UINTN
SfbBlsLength (const char *Text)
{
  SFB_UINTN Index = 0;

  while (Text[Index] != '\0') {
    Index++;
  }
  return Index;
}

static void
SfbBlsZero (void *Buffer, SFB_UINTN Bytes)
{
  unsigned char *Out = (unsigned char *)Buffer;
  SFB_UINTN      Index;

  for (Index = 0; Index < Bytes; Index++) {
    Out[Index] = 0;
  }
}

/*
 * One logical line: [Begin, End) with the terminator, any '\r' before it and
 * surrounding whitespace already removed. *Cursor advances past the newline.
 * Returns FALSE only when the buffer is exhausted.
 */
static SFB_BOOLEAN
SfbBlsNextLine (const char **Cursor,
                const char  *Limit,
                const char **Begin,
                const char **End)
{
  const char *Scan = *Cursor;
  const char *LineStart;
  const char *LineEnd;

  if (Scan >= Limit || *Scan == '\0') {
    return FALSE;
  }

  LineStart = Scan;
  while (Scan < Limit && *Scan != '\0' && *Scan != '\n') {
    Scan++;
  }
  LineEnd = Scan;
  if (Scan < Limit && *Scan == '\n') {
    Scan++;
  }
  *Cursor = Scan;

  if (LineEnd > LineStart && LineEnd[-1] == '\r') {
    LineEnd--;
  }
  while (LineStart < LineEnd && SfbBlsIsSpace (*LineStart)) {
    LineStart++;
  }
  while (LineEnd > LineStart && SfbBlsIsSpace (LineEnd[-1])) {
    LineEnd--;
  }

  *Begin = LineStart;
  *End = LineEnd;
  return TRUE;
}

/*
 * Split a line into a key and a value. The key is the run up to the first
 * space or tab; the value is what follows with its leading whitespace dropped.
 * A line with no whitespace is all key and an empty value.
 */
static void
SfbBlsSplit (const char  *Begin,
             const char  *End,
             const char **KeyEnd,
             const char **ValueBegin)
{
  const char *Scan = Begin;

  while (Scan < End && !SfbBlsIsSpace (*Scan)) {
    Scan++;
  }
  *KeyEnd = Scan;
  while (Scan < End && SfbBlsIsSpace (*Scan)) {
    Scan++;
  }
  *ValueBegin = Scan;
}

static SFB_BOOLEAN
SfbBlsKeyIs (const char *Begin, const char *End, const char *Want)
{
  SFB_UINTN Length = (SFB_UINTN)(End - Begin);
  SFB_UINTN Index;

  if (Length != SfbBlsLength (Want)) {
    return FALSE;
  }
  for (Index = 0; Index < Length; Index++) {
    if (Begin[Index] != Want[Index]) {
      return FALSE;
    }
  }
  return TRUE;
}

/*
 * Copy Text[0..Length) and terminate. FALSE when it did not fit, leaving Out
 * exactly as it was.
 */
static SFB_BOOLEAN
SfbBlsCopy (char *Out, SFB_UINTN Chars, const char *Text, SFB_UINTN Length)
{
  SFB_UINTN Index;

  if (Out == NULL || Chars == 0 || Length >= Chars) {
    return FALSE;
  }
  for (Index = 0; Index < Length; Index++) {
    Out[Index] = Text[Index];
  }
  Out[Length] = '\0';
  return TRUE;
}

/*
 * A path value, normalised to the separator the firmware's file protocol
 * wants. The spec writes '/' because it is written on Linux; every consumer
 * here is EFI_FILE_PROTOCOL, which wants '\'. Rejects an empty value and one
 * that does not start at the volume root, because a relative path has no
 * defined base once the entry has been lifted out of its directory.
 */
static SFB_BOOLEAN
SfbBlsCopyPath (char *Out, SFB_UINTN Chars, const char *Text, SFB_UINTN Length)
{
  SFB_UINTN Index;

  if (Length == 0 || Length >= Chars) {
    return FALSE;
  }
  if (Text[0] != '/' && Text[0] != '\\') {
    return FALSE;
  }
  for (Index = 0; Index < Length; Index++) {
    Out[Index] = (Text[Index] == '/') ? '\\' : Text[Index];
  }
  Out[Length] = '\0';
  return TRUE;
}

/*
 * `options` may appear more than once and the spec says the values are
 * concatenated. Join with exactly one space; a repeat that does not fit is
 * rejected whole rather than truncated, because half a kernel command line is
 * worse than none.
 */
static SFB_BOOLEAN
SfbBlsAppendCmdline (char *Out, SFB_UINTN Chars, const char *Text,
                     SFB_UINTN Length)
{
  SFB_UINTN Used = SfbBlsLength (Out);
  SFB_UINTN Index;
  SFB_UINTN Needed;

  if (Length == 0) {
    return FALSE;
  }
  Needed = Used + Length + ((Used != 0) ? 1u : 0u);
  if (Needed >= Chars) {
    return FALSE;
  }
  if (Used != 0) {
    Out[Used++] = ' ';
  }
  for (Index = 0; Index < Length; Index++) {
    Out[Used + Index] = Text[Index];
  }
  Out[Used + Length] = '\0';
  return TRUE;
}

SFB_BOOLEAN
SfbBlsParse (const char *Bytes, SFB_UINTN Size, SFB_BLS_ENTRY *Entry)
{
  const char *Cursor;
  const char *Limit;
  const char *Begin;
  const char *End;
  SFB_BOOLEAN HasLinux = FALSE;
  SFB_BOOLEAN HasEfi = FALSE;
  SFB_BOOLEAN HasInitrd = FALSE;
  SFB_BOOLEAN HasDtb = FALSE;

  if (Entry == NULL) {
    return FALSE;
  }
  SfbBlsZero (Entry, sizeof (*Entry));
  if (Bytes == NULL || Size == 0) {
    return FALSE;
  }
  if (Size > SFB_BLS_MAX_BYTES) {
    Size = SFB_BLS_MAX_BYTES;
  }

  Cursor = Bytes;
  Limit = Bytes + Size;

  while (SfbBlsNextLine (&Cursor, Limit, &Begin, &End)) {
    const char *KeyEnd;
    const char *Value;
    SFB_UINTN   ValueLength;

    if (Begin == End || *Begin == '#') {
      continue;
    }

    SfbBlsSplit (Begin, End, &KeyEnd, &Value);
    ValueLength = (SFB_UINTN)(End - Value);

    if (SfbBlsKeyIs (Begin, KeyEnd, "title")) {
      if (!SfbBlsCopy (Entry->Title, SFB_BLS_TITLE_CHARS, Value,
                       ValueLength)) {
        Entry->RejectedLines++;
      }
      continue;
    }

    if (SfbBlsKeyIs (Begin, KeyEnd, "linux")) {
      /* Two image keys in one file is an authoring error the parser must not
       * resolve on the author's behalf; the whole entry is refused below. */
      if (HasLinux || !SfbBlsCopyPath (Entry->Image, SFB_BLS_PATH_CHARS, Value,
                                       ValueLength)) {
        Entry->RejectedLines++;
        continue;
      }
      HasLinux = TRUE;
      continue;
    }

    if (SfbBlsKeyIs (Begin, KeyEnd, "efi")) {
      if (HasEfi || !SfbBlsCopyPath (Entry->Image, SFB_BLS_PATH_CHARS, Value,
                                     ValueLength)) {
        Entry->RejectedLines++;
        continue;
      }
      HasEfi = TRUE;
      continue;
    }

    if (SfbBlsKeyIs (Begin, KeyEnd, "initrd")) {
      /* The spec allows several; this loader publishes exactly one LoadFile2
       * handle, so only the first is honoured and the rest are counted. */
      if (HasInitrd || !SfbBlsCopyPath (Entry->Initrd, SFB_BLS_PATH_CHARS,
                                        Value, ValueLength)) {
        Entry->RejectedLines++;
        continue;
      }
      HasInitrd = TRUE;
      continue;
    }

    if (SfbBlsKeyIs (Begin, KeyEnd, "devicetree")) {
      if (HasDtb || !SfbBlsCopyPath (Entry->Dtb, SFB_BLS_PATH_CHARS, Value,
                                     ValueLength)) {
        Entry->RejectedLines++;
        continue;
      }
      HasDtb = TRUE;
      continue;
    }

    if (SfbBlsKeyIs (Begin, KeyEnd, "options")) {
      if (!SfbBlsAppendCmdline (Entry->Cmdline, SFB_BLS_CMDLINE_CHARS, Value,
                                ValueLength)) {
        Entry->RejectedLines++;
      }
      continue;
    }

    /* Every other key the spec defines - version, machine-id, sort-key,
     * architecture and the rest - is presentation or selection metadata this
     * loader has no use for. Ignoring them is required: rejecting them would
     * make an ordinary distribution-written file unbootable. */
  }

  if (HasLinux == HasEfi) {
    /* Neither: nothing to launch. Both: the author asked for two different
     * things and the parser is not entitled to pick one. */
    return FALSE;
  }

  Entry->Kind = HasLinux ? SfbBlsKindLinux : SfbBlsKindEfi;
  if (Entry->Kind == SfbBlsKindEfi) {
    /* An `efi` entry is a plain application launch: an initrd and a device
     * tree would be published for a kernel that is never loaded. Refuse them
     * loudly rather than silently publishing state nothing consumes. */
    if (Entry->Initrd[0] != '\0' || Entry->Dtb[0] != '\0') {
      Entry->RejectedLines++;
      Entry->Initrd[0] = '\0';
      Entry->Dtb[0] = '\0';
    }
  }
  return TRUE;
}
