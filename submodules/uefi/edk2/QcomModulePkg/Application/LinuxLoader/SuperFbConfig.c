/*
 * Pure `canoe.cfg` parser. See SuperFbConfig.h and wiki/docs/canoe-cfg.md.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbConfig.h"

/* Keeps the translation unit legal when the feature is compiled out. */
const char *gSfbConfigModuleTag = "SuperFbConfig";

static SFB_BOOLEAN
SfbCfgIsSpace (char Character)
{
  return (SFB_BOOLEAN)(Character == ' ' || Character == '\t');
}

static SFB_BOOLEAN
SfbCfgIsPrintable (char Character)
{
  unsigned char Byte = (unsigned char)Character;

  return (SFB_BOOLEAN)(Byte >= 0x20u && Byte <= 0x7eu);
}

static SFB_UINTN
SfbCfgLength (const char *Text)
{
  SFB_UINTN Index = 0;

  while (Text[Index] != '\0') {
    Index++;
  }
  return Index;
}

static SFB_BOOLEAN
SfbCfgEquals (const char *Left, const char *Right)
{
  SFB_UINTN Index;

  for (Index = 0; Left[Index] == Right[Index]; Index++) {
    if (Left[Index] == '\0') {
      return TRUE;
    }
  }
  return FALSE;
}

static void
SfbCfgZero (void *Buffer, SFB_UINTN Bytes)
{
  unsigned char *Out = (unsigned char *)Buffer;
  SFB_UINTN      Index;

  for (Index = 0; Index < Bytes; Index++) {
    Out[Index] = 0;
  }
}

/*
 * Copy Text[0..Length) and terminate. FALSE when it did not fit, leaving Out
 * exactly as it was: a rejected value must not destroy the one already there,
 * or an over-long `title` would blank a row that had a perfectly good default.
 * Callers that want a rejection to clear the field do so themselves.
 */
static SFB_BOOLEAN
SfbCfgCopy (char *Out, SFB_UINTN Chars, const char *Text, SFB_UINTN Length)
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
 * Decimal only, no sign, no leading-plus, bounded by Limit. Rejects an empty
 * run and anything that would overflow before the bound is even reached.
 */
static SFB_BOOLEAN
SfbCfgParseU32 (const char *Text,
                SFB_UINTN   Length,
                SFB_UINT32  Limit,
                SFB_UINT32 *Value)
{
  SFB_UINT32 Accumulated = 0;
  SFB_UINTN  Index;

  if (Length == 0 || Length > 10) {
    return FALSE;
  }
  for (Index = 0; Index < Length; Index++) {
    char Digit = Text[Index];

    if (Digit < '0' || Digit > '9') {
      return FALSE;
    }
    Accumulated = (Accumulated * 10u) + (SFB_UINT32)(Digit - '0');
    if (Accumulated > Limit) {
      return FALSE;
    }
  }
  *Value = Accumulated;
  return TRUE;
}

/* An id is 1..SFB_CONFIG_ID_CHARS-1 characters of [A-Za-z0-9._-]. The set is
 * deliberately narrow: an id is compared against `default` and printed, and a
 * separator or a space inside one would make both ambiguous. */
static SFB_BOOLEAN
SfbCfgValidId (const char *Text, SFB_UINTN Length)
{
  SFB_UINTN Index;

  if (Length == 0 || Length >= SFB_CONFIG_ID_CHARS) {
    return FALSE;
  }
  for (Index = 0; Index < Length; Index++) {
    char Character = Text[Index];

    if ((Character >= 'A' && Character <= 'Z') ||
        (Character >= 'a' && Character <= 'z') ||
        (Character >= '0' && Character <= '9') ||
        Character == '.' || Character == '_' || Character == '-') {
      continue;
    }
    return FALSE;
  }
  return TRUE;
}

/*
 * Fold a boot-root-relative path into an absolute one on the boot root:
 * one optional leading separator is dropped, '/' becomes '\', and the result
 * carries exactly one leading '\'.
 *
 * The rejected shapes are the ones normalization would otherwise hide: an
 * empty path, a '.' or '..' component, a doubled separator and a trailing
 * separator. They are refused before normalization, so no two distinct inputs
 * can canonicalise onto the same accepted path.
 */
static SFB_BOOLEAN
SfbCfgFoldPath (const char *Text,
                SFB_UINTN   Length,
                char       *Out,
                SFB_UINTN   Chars)
{
  SFB_UINTN Index;
  SFB_UINTN Count = 0;
  SFB_UINTN Start;
  SFB_UINTN ComponentStart;

  Start = 0;
  if (Start < Length && (Text[Start] == '/' || Text[Start] == '\\')) {
    Start++;
  }
  if (Start >= Length) {
    return FALSE;
  }
  if (Text[Length - 1] == '/' || Text[Length - 1] == '\\') {
    return FALSE;
  }

  /* Component walk over the un-normalised text: a separator of either flavour
   * ends a component. */
  ComponentStart = Start;
  for (Index = Start; Index <= Length; Index++) {
    SFB_BOOLEAN AtEnd = (SFB_BOOLEAN)(Index == Length);
    SFB_UINTN   Bytes;

    if (!AtEnd && Text[Index] != '/' && Text[Index] != '\\') {
      continue;
    }
    Bytes = Index - ComponentStart;
    if (Bytes == 0) {
      return FALSE;
    }
    if (Bytes == 1 && Text[ComponentStart] == '.') {
      return FALSE;
    }
    if (Bytes == 2 && Text[ComponentStart] == '.' &&
        Text[ComponentStart + 1] == '.') {
      return FALSE;
    }
    if (AtEnd) {
      break;
    }
    ComponentStart = Index + 1;
  }

  if (Chars < 3) {
    return FALSE;
  }
  Out[Count++] = '\\';
  for (Index = Start; Index < Length; Index++) {
    char Character = Text[Index];

    if (!SfbCfgIsPrintable (Character)) {
      Out[0] = '\0';
      return FALSE;
    }
    if (Character == '/') {
      Character = '\\';
    }
    if (Count + 1 >= Chars) {
      Out[0] = '\0';
      return FALSE;
    }
    Out[Count++] = Character;
  }
  Out[Count] = '\0';
  return TRUE;
}

/*
 * One logical line: [Begin, End) with the terminator, any '\r' before it and
 * surrounding whitespace already removed. *Cursor advances past the newline.
 * Returns FALSE only when the buffer is exhausted.
 */
static SFB_BOOLEAN
SfbCfgNextLine (const char **Cursor,
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
  while (LineStart < LineEnd && SfbCfgIsSpace (*LineStart)) {
    LineStart++;
  }
  while (LineEnd > LineStart && SfbCfgIsSpace (LineEnd[-1])) {
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
SfbCfgSplit (const char  *Begin,
             const char  *End,
             const char **KeyEnd,
             const char **ValueBegin)
{
  const char *Scan = Begin;

  while (Scan < End && !SfbCfgIsSpace (*Scan)) {
    Scan++;
  }
  *KeyEnd = Scan;
  while (Scan < End && SfbCfgIsSpace (*Scan)) {
    Scan++;
  }
  *ValueBegin = Scan;
}

static SFB_BOOLEAN
SfbCfgKeyIs (const char *Begin, const char *End, const char *Want)
{
  SFB_UINTN Length = (SFB_UINTN)(End - Begin);
  SFB_UINTN Index;

  if (Length != SfbCfgLength (Want)) {
    return FALSE;
  }
  for (Index = 0; Index < Length; Index++) {
    if (Begin[Index] != Want[Index]) {
      return FALSE;
    }
  }
  return TRUE;
}

static SFB_BOOLEAN
SfbCfgParseMode (const char *Begin, const char *End, SFB_UINT8 *Mode)
{
  SFB_UINT32 Value;

  if (!SfbCfgParseU32 (Begin, (SFB_UINTN)(End - Begin), SFB_CONFIG_MODE_MAX,
                       &Value)) {
    return FALSE;
  }
  *Mode = (SFB_UINT8)Value;
  return TRUE;
}

static SFB_BOOLEAN
SfbCfgParseRole (const char      *Begin,
                 const char      *End,
                 SFB_CONFIG_ROLE *Role)
{
  if (SfbCfgKeyIs (Begin, End, "active")) {
    *Role = SfbConfigRoleActive;
    return TRUE;
  }
  if (SfbCfgKeyIs (Begin, End, "inactive")) {
    *Role = SfbConfigRoleInactive;
    return TRUE;
  }
  if (SfbCfgKeyIs (Begin, End, "backup")) {
    *Role = SfbConfigRoleBackup;
    return TRUE;
  }
  if (SfbCfgKeyIs (Begin, End, "other")) {
    *Role = SfbConfigRoleOther;
    return TRUE;
  }
  return FALSE;
}

/* TRUE when Id[0..Length) is already the id of an accepted entry. Compared
 * against the stored NUL-terminated id, so a prefix never collides. */
static SFB_BOOLEAN
SfbCfgIdTaken (const SFB_CONFIG *Config, const char *Id, SFB_UINTN Length)
{
  SFB_UINTN Index;

  for (Index = 0; Index < Config->Count; Index++) {
    const char *Existing = Config->Entry[Index].Id;
    SFB_UINTN   Scan;

    if (SfbCfgLength (Existing) != Length) {
      continue;
    }
    for (Scan = 0; Scan < Length && Existing[Scan] == Id[Scan]; Scan++) {
    }
    if (Scan == Length) {
      return TRUE;
    }
  }
  return FALSE;
}

SFB_UINT8
SfbConfigEntryMode (
  const SFB_CONFIG       *Config,
  const SFB_CONFIG_ENTRY *Entry
  )
{
  if (Entry == NULL) {
    return (Config != NULL) ? Config->Mode : (SFB_UINT8)SFB_CONFIG_MODE_FAKE_LOCKED;
  }
  if (Entry->ModeExplicit) {
    return Entry->Mode;
  }
  return (Config != NULL) ? Config->Mode : (SFB_UINT8)SFB_CONFIG_MODE_FAKE_LOCKED;
}

const char *
SfbConfigRoleSuffix (SFB_CONFIG_ROLE Role)
{
  switch (Role) {
  case SfbConfigRoleActive:
    return " (active)";
  case SfbConfigRoleInactive:
    return " (inactive)";
  case SfbConfigRoleBackup:
    return " (backup)";
  case SfbConfigRoleOther:
  default:
    return "";
  }
}

SFB_BOOLEAN
SfbConfigParse (
  const char *Bytes,
  SFB_UINTN   Size,
  SFB_CONFIG *Config
  )
{
  const char       *Cursor;
  const char       *Limit;
  const char       *Begin;
  const char       *End;
  SFB_BOOLEAN       SawVersion = FALSE;
  SFB_CONFIG_ENTRY *Current = NULL;
  char              Default[SFB_CONFIG_ID_CHARS];
  SFB_UINTN         Index;

  if (Config == NULL) {
    return FALSE;
  }
  SfbCfgZero (Config, sizeof (*Config));
  Config->Mode = (SFB_UINT8)SFB_CONFIG_MODE_FAKE_LOCKED;
  Config->TimeoutSeconds = SFB_CONFIG_DEFAULT_TIMEOUT;
  Config->LockPolicy = SfbConfigLockAsNeeded;
  Config->DefaultIndex = SFB_CONFIG_NO_DEFAULT;
  Default[0] = '\0';

  if (Bytes == NULL || Size == 0) {
    return FALSE;
  }
  if (Size > SFB_CONFIG_MAX_BYTES) {
    Size = SFB_CONFIG_MAX_BYTES;
  }

  Cursor = Bytes;
  Limit = Bytes + Size;

  while (SfbCfgNextLine (&Cursor, Limit, &Begin, &End)) {
    const char *KeyEnd;
    const char *Value;
    SFB_UINTN   ValueLength;

    if (Begin == End || *Begin == '#') {
      continue;
    }
    SfbCfgSplit (Begin, End, &KeyEnd, &Value);
    ValueLength = (SFB_UINTN)(End - Value);

    if (SfbCfgKeyIs (Begin, KeyEnd, "version")) {
      SFB_UINT32 Parsed = 0;

      if (SawVersion ||
          !SfbCfgParseU32 (Value, ValueLength, SFB_CONFIG_VERSION, &Parsed) ||
          Parsed != SFB_CONFIG_VERSION) {
        /* An unknown generation of this file must not be half-applied. */
        SfbCfgZero (Config, sizeof (*Config));
        return FALSE;
      }
      SawVersion = TRUE;
      continue;
    }

    /* Nothing else is believed until the version is established: a file whose
     * first lines parse under a future grammar must not leak into this one. */
    if (!SawVersion) {
      Config->RejectedLines++;
      continue;
    }

    if (SfbCfgKeyIs (Begin, KeyEnd, "entry")) {
      if (Config->Count >= SFB_CONFIG_MAX_ENTRIES ||
          !SfbCfgValidId (Value, ValueLength) ||
          SfbCfgIdTaken (Config, Value, ValueLength)) {
        Config->RejectedLines++;
        Current = NULL;
        continue;
      }
      Current = &Config->Entry[Config->Count];
      SfbCfgZero (Current, sizeof (*Current));
      Current->Role = SfbConfigRoleOther;
      Current->Mode = Config->Mode;
      (void)SfbCfgCopy (Current->Id, SFB_CONFIG_ID_CHARS, Value, ValueLength);
      /* Title defaults to the id; an explicit `title` replaces it. */
      (void)SfbCfgCopy (Current->Title, SFB_CONFIG_TITLE_CHARS, Value,
                        ValueLength);
      Config->Count++;
      continue;
    }

    if (Current != NULL) {
      if (SfbCfgKeyIs (Begin, KeyEnd, "title")) {
        if (ValueLength == 0 ||
            !SfbCfgCopy (Current->Title, SFB_CONFIG_TITLE_CHARS, Value,
                         ValueLength)) {
          Config->RejectedLines++;
        }
        continue;
      }
      if (SfbCfgKeyIs (Begin, KeyEnd, "image")) {
        if (!SfbCfgFoldPath (Value, ValueLength, Current->Image,
                             SFB_CONFIG_PATH_CHARS)) {
          Config->RejectedLines++;
        }
        continue;
      }
      if (SfbCfgKeyIs (Begin, KeyEnd, "mode")) {
        if (!SfbCfgParseMode (Value, End, &Current->Mode)) {
          Current->Mode = Config->Mode;
          Config->RejectedLines++;
        } else {
          Current->ModeExplicit = TRUE;
        }
        continue;
      }
      if (SfbCfgKeyIs (Begin, KeyEnd, "role")) {
        if (!SfbCfgParseRole (Value, End, &Current->Role)) {
          Config->RejectedLines++;
        }
        continue;
      }
      Config->RejectedLines++;
      continue;
    }

    /* File-global keys. Reached only before the first `entry`. */
    if (SfbCfgKeyIs (Begin, KeyEnd, "generation")) {
      if (!SfbCfgParseU32 (Value, ValueLength, 0xffffffffu,
                           &Config->Generation)) {
        Config->RejectedLines++;
      }
      continue;
    }
    if (SfbCfgKeyIs (Begin, KeyEnd, "timeout")) {
      if (!SfbCfgParseU32 (Value, ValueLength, SFB_CONFIG_TIMEOUT_MAX,
                           &Config->TimeoutSeconds)) {
        Config->TimeoutSeconds = SFB_CONFIG_DEFAULT_TIMEOUT;
        Config->RejectedLines++;
      }
      continue;
    }
    if (SfbCfgKeyIs (Begin, KeyEnd, "default")) {
      if (!SfbCfgValidId (Value, ValueLength) ||
          !SfbCfgCopy (Default, SFB_CONFIG_ID_CHARS, Value, ValueLength)) {
        Default[0] = '\0';
        Config->RejectedLines++;
      }
      continue;
    }
    if (SfbCfgKeyIs (Begin, KeyEnd, "mode")) {
      if (!SfbCfgParseMode (Value, End, &Config->Mode)) {
        Config->Mode = (SFB_UINT8)SFB_CONFIG_MODE_FAKE_LOCKED;
        Config->RejectedLines++;
      }
      continue;
    }
    if (SfbCfgKeyIs (Begin, KeyEnd, "lockstate")) {
      if (SfbCfgKeyIs (Value, End, "never")) {
        Config->LockPolicy = SfbConfigLockNever;
      } else if (SfbCfgKeyIs (Value, End, "asneeded")) {
        Config->LockPolicy = SfbConfigLockAsNeeded;
      } else {
        Config->RejectedLines++;
      }
      continue;
    }

    Config->RejectedLines++;
  }

  if (!SawVersion) {
    SfbCfgZero (Config, sizeof (*Config));
    return FALSE;
  }

  /* Drop entries with no usable image. Done in one compaction pass so an
   * earlier rejection cannot renumber a later `default` resolution. */
  {
    SFB_UINTN Keep = 0;

    for (Index = 0; Index < Config->Count; Index++) {
      if (Config->Entry[Index].Image[0] == '\0') {
        Config->RejectedLines++;
        continue;
      }
      if (Keep != Index) {
        Config->Entry[Keep] = Config->Entry[Index];
      }
      Keep++;
    }
    for (Index = Keep; Index < Config->Count; Index++) {
      SfbCfgZero (&Config->Entry[Index], sizeof (Config->Entry[Index]));
    }
    Config->Count = Keep;
  }

  if (Config->Count == 0) {
    SfbCfgZero (Config, sizeof (*Config));
    return FALSE;
  }

  if (Default[0] != '\0') {
    for (Index = 0; Index < Config->Count; Index++) {
      if (SfbCfgEquals (Config->Entry[Index].Id, Default)) {
        Config->DefaultIndex = Index;
        break;
      }
    }
    if (Config->DefaultIndex == SFB_CONFIG_NO_DEFAULT) {
      /* A default naming an entry that is not installed is a real condition
       * after a partial OTA, not a reason to refuse to boot. */
      Config->RejectedLines++;
    }
  }

  Config->Valid = TRUE;
  return TRUE;
}
