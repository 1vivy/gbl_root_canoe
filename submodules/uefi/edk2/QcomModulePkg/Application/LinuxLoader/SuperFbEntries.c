/*
 * Boot entry list, persistence and launching for the super-fastboot boot menu.
 *
 * Two records in the ESP tail store back the menu (see SuperFbStore.c):
 *   slot SFB_STORE_DEFAULT - the entry the 5 second timeout launches
 *   slot SFB_STORE_CUSTOM  - the single user-added entry, from the file browser
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbMenu.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include "Hook/HookCommon.h"
#include "Hook/SuperFbManagedPath.h"
#include "Hook/SuperFbProfile.h"
#include "SuperFbLaunchPolicy.h"

/* Keeps the translation unit legal when the feature is compiled out. */
CONST CHAR8 *gSfbEntriesModuleTag = "SuperFbEntries";

/*
 * A stored entry is one line of ASCII:
 *
 *   SFB1|<volume label>|<path on volume>|<description>
 *
 * The volume is named by its FAT label rather than by a serialised device path
 * because handle order and device paths are not stable across a reboot, a
 * firmware update or a change of storage controller, whereas the label written
 * into the file system is. The label is a hint: if no volume carries it, any
 * volume holding the same path will do.
 */
#define SFB_RECORD_TAG    "SFB1"
#define SFB_RECORD_FIELD  '|'

VOID
SfbFreeEntry (IN OUT SFB_BOOT_ENTRY *Entry)
{
  if (Entry->DevicePath != NULL) {
    FreePool (Entry->DevicePath);
    Entry->DevicePath = NULL;
  }
}

STATIC
BOOLEAN
SfbIsCanonicalPath (IN CONST CHAR16 *Path)
{
  UINTN        Index;
  UINTN        ComponentStart;
  UINTN        ComponentBytes;

  if (Path == NULL || Path[0] != L'\\' || Path[1] == L'\0' ||
      Path[1] == L'\\') {
    return FALSE;
  }

  ComponentStart = 1;
  for (Index = 1; ; Index++) {
    if (Path[Index] != L'\\' && Path[Index] != L'\0') {
      continue;
    }

    ComponentBytes = Index - ComponentStart;
    if (ComponentBytes == 0 || (ComponentBytes == 1 &&
                                Path[ComponentStart] == L'.') ||
        (ComponentBytes == 2 && Path[ComponentStart] == L'.' &&
         Path[ComponentStart + 1] == L'.')) {
      return FALSE;
    }
    if (Path[Index] == L'\0') {
      return TRUE;
    }
    ComponentStart = Index + 1;
  }
}

EFI_STATUS
SfbMakeFileEntry (IN EFI_HANDLE      Volume,
                  IN CONST CHAR16    *PathOnVolume,
                  IN CONST CHAR16    *Desc,
                  OUT SFB_BOOT_ENTRY *Entry)
{
  EFI_FILE_PROTOCOL  *Root = NULL;

  if (Entry == NULL || !SfbIsCanonicalPath (PathOnVolume)) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Entry, sizeof (*Entry));

  Entry->Kind = SfbEntryEfiFile;
  Entry->Volume = Volume;
  StrnCpyS (Entry->Path, SFB_PATH_CHARS, PathOnVolume, SFB_PATH_CHARS - 1);
  StrnCpyS (Entry->Desc, SFB_DESC_CHARS, Desc, SFB_DESC_CHARS - 1);

  /* Recorded now, while the volume is in hand, so saving the entry later does
   * not have to reopen it. */
  if (!EFI_ERROR (SfbOpenVolumeRoot (Volume, &Root)) && Root != NULL) {
    SfbGetVolumeLabel (Root, Entry->VolLabel, SFB_DESC_CHARS);
    Root->Close (Root);
  }

  Entry->DevicePath = FileDevicePath (Volume, PathOnVolume);
  if (Entry->DevicePath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  return EFI_SUCCESS;
}

/* ---- record encoding ---------------------------------------------------- */

/*
 * Stored records are a printable 7-bit format. Reject an entry before
 * serialization rather than silently dropping Unicode or field separators and
 * persisting a path that names a different file after reboot.
 */
STATIC
BOOLEAN
SfbCanEncodeRecordField (IN CONST CHAR16 *Text)
{
  UINTN Index;

  if (Text == NULL) {
    return FALSE;
  }
  for (Index = 0; Text[Index] != L'\0'; ++Index) {
    if (Text[Index] < 0x20 || Text[Index] > 0x7e ||
        Text[Index] == SFB_RECORD_FIELD) {
      return FALSE;
    }
  }
  return TRUE;
}

STATIC
VOID
SfbAppendAscii (IN OUT CHAR8    *Buffer,
                IN UINTN        BufferBytes,
                IN CONST CHAR16 *Text)
{
  UINTN  Out = AsciiStrLen (Buffer);
  UINTN  Index;

  for (Index = 0; Text[Index] != L'\0' && Out + 1 < BufferBytes; Index++) {
    CHAR16  Ch = Text[Index];

    if (Ch < 0x20 || Ch > 0x7e || Ch == SFB_RECORD_FIELD) {
      continue;
    }
    Buffer[Out++] = (CHAR8)Ch;
  }

  Buffer[Out] = '\0';
}

STATIC
VOID
SfbAppendSeparator (IN OUT CHAR8 *Buffer, IN UINTN BufferBytes)
{
  UINTN  Out = AsciiStrLen (Buffer);

  if (Out + 1 < BufferBytes) {
    Buffer[Out] = SFB_RECORD_FIELD;
    Buffer[Out + 1] = '\0';
  }
}

/* Copy one field out of Record into a Unicode buffer, and return where the
 * next field starts. */
STATIC
CONST CHAR8 *
SfbTakeField (IN CONST CHAR8 *Record, OUT CHAR16 *Out, IN UINTN OutChars)
{
  UINTN  Index = 0;

  while (Record[Index] != '\0' && Record[Index] != SFB_RECORD_FIELD) {
    if (Index + 1 < OutChars) {
      Out[Index] = (CHAR16)Record[Index];
    }
    Index++;
  }

  Out[(Index < OutChars - 1) ? Index : OutChars - 1] = L'\0';

  return (Record[Index] == SFB_RECORD_FIELD) ? &Record[Index + 1]
                                             : &Record[Index];
}

/* ---- persistence -------------------------------------------------------- */

STATIC
EFI_STATUS
SfbSaveEntryRecord (IN UINTN Slot, IN CONST SFB_BOOT_ENTRY *Entry)
{
  CHAR8  Record[SFB_STORE_SLOT_BYTES];

  if (Entry->Kind != SfbEntryEfiFile || Entry->Path[0] == L'\0' ||
      !SfbCanEncodeRecordField (Entry->VolLabel) ||
      !SfbCanEncodeRecordField (Entry->Path) ||
      !SfbCanEncodeRecordField (Entry->Desc)) {
    return EFI_UNSUPPORTED;
  }

  AsciiStrCpyS (Record, sizeof (Record), SFB_RECORD_TAG);
  SfbAppendSeparator (Record, sizeof (Record));
  SfbAppendAscii (Record, sizeof (Record), Entry->VolLabel);
  SfbAppendSeparator (Record, sizeof (Record));
  SfbAppendAscii (Record, sizeof (Record), Entry->Path);
  SfbAppendSeparator (Record, sizeof (Record));
  SfbAppendAscii (Record, sizeof (Record), Entry->Desc);

  DEBUG ((EFI_D_INFO, "SFB: store slot %u <- '%a'\n", (UINT32)Slot, Record));

  return SfbStoreWrite (Slot, Record);
}

/*
 * Turn a stored record back into a usable entry by finding a live volume for
 * it. The label decides between candidates; the path decides whether a volume
 * is a candidate at all, so an entry whose image has been deleted stays gone
 * rather than resolving onto the wrong disk.
 */
STATIC
EFI_STATUS
SfbResolveRecord (IN CONST CHAR16    *WantLabel,
                  IN CONST CHAR16    *Path,
                  IN CONST CHAR16    *Desc,
                  OUT SFB_BOOT_ENTRY *Entry)
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Volumes = NULL;
  UINTN       VolumeCount = 0;
  UINTN       Index;
  EFI_HANDLE  Fallback = NULL;
  EFI_HANDLE  Chosen = NULL;

  ZeroMem (Entry, sizeof (*Entry));

  Status = SfbLocateVolumes (&Volumes, &VolumeCount);
  if (EFI_ERROR (Status) || Volumes == NULL) {
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < VolumeCount && Chosen == NULL; Index++) {
    EFI_FILE_PROTOCOL  *Root = NULL;
    CHAR16             Label[SFB_DESC_CHARS];

    if (EFI_ERROR (SfbOpenVolumeRoot (Volumes[Index], &Root)) || Root == NULL) {
      continue;
    }

    if (SfbFileExists (Root, Path)) {
      SfbGetVolumeLabel (Root, Label, SFB_DESC_CHARS);

      if (WantLabel[0] != L'\0' && StrCmp (Label, WantLabel) == 0) {
        Chosen = Volumes[Index];
      } else if (Fallback == NULL) {
        Fallback = Volumes[Index];
      }
    }

    Root->Close (Root);
  }

  if (Chosen == NULL) {
    Chosen = Fallback;
  }

  FreePool (Volumes);

  if (Chosen == NULL) {
    return EFI_NOT_FOUND;
  }

  Status = SfbMakeFileEntry (Chosen, Path, Desc, Entry);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  /* Keep the label that was stored: it is what the record will be rewritten
   * with, and an unlabelled fallback volume should not overwrite it. */
  if (WantLabel[0] != L'\0') {
    StrnCpyS (Entry->VolLabel, SFB_DESC_CHARS, WantLabel, SFB_DESC_CHARS - 1);
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
SfbLoadEntryRecord (IN UINTN Slot, OUT SFB_BOOT_ENTRY *Entry)
{
  EFI_STATUS   Status;
  CHAR8        Record[SFB_STORE_SLOT_BYTES];
  CONST CHAR8  *Cursor;
  CHAR16       Tag[8];
  CHAR16       Label[SFB_DESC_CHARS];
  CHAR16       Path[SFB_PATH_CHARS];
  CHAR16       Desc[SFB_DESC_CHARS];

  ZeroMem (Entry, sizeof (*Entry));

  Status = SfbStoreRead (Slot, Record, sizeof (Record));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (Record[0] == '\0') {
    return EFI_NOT_FOUND;
  }

  Cursor = SfbTakeField (Record, Tag, ARRAY_SIZE (Tag));
  if (StrCmp (Tag, L"SFB1") != 0) {
    DEBUG ((EFI_D_ERROR, "SFB: store slot %u is not a record\n", (UINT32)Slot));
    return EFI_VOLUME_CORRUPTED;
  }

  Cursor = SfbTakeField (Cursor, Label, SFB_DESC_CHARS);
  Cursor = SfbTakeField (Cursor, Path, SFB_PATH_CHARS);
  SfbTakeField (Cursor, Desc, SFB_DESC_CHARS);

  /* A path has to be absolute; anything else would be interpreted relative to
   * the volume root by Open () and is more likely corruption than intent. */
  if (Path[0] != L'\\') {
    DEBUG ((EFI_D_ERROR, "SFB: store slot %u has a bad path\n", (UINT32)Slot));
    return EFI_VOLUME_CORRUPTED;
  }

  return SfbResolveRecord (Label, Path, Desc, Entry);
}

EFI_STATUS
SfbSaveDefaultEntry (IN CONST SFB_BOOT_ENTRY *Entry)
{
  return SfbSaveEntryRecord (SFB_STORE_DEFAULT, Entry);
}

EFI_STATUS
SfbSaveCustomEntry (IN CONST SFB_BOOT_ENTRY *Entry)
{
  return SfbSaveEntryRecord (SFB_STORE_CUSTOM, Entry);
}

/* ---- menu construction -------------------------------------------------- */

STATIC
BOOLEAN
SfbSameDevicePath (IN CONST EFI_DEVICE_PATH_PROTOCOL *A,
                   IN CONST EFI_DEVICE_PATH_PROTOCOL *B)
{
  UINTN  SizeA;
  UINTN  SizeB;

  if (A == NULL || B == NULL) {
    return FALSE;
  }

  SizeA = GetDevicePathSize ((EFI_DEVICE_PATH_PROTOCOL *)A);
  SizeB = GetDevicePathSize ((EFI_DEVICE_PATH_PROTOCOL *)B);

  return (BOOLEAN)(SizeA == SizeB && CompareMem (A, B, SizeA) == 0);
}

STATIC
VOID
SfbAppendBuiltIn (IN OUT SFB_MENU_STATE *Menu,
                  IN SFB_ENTRY_KIND     Kind,
                  IN CONST CHAR16       *Desc)
{
  SFB_BOOT_ENTRY  *Entry;

  if (Menu->Count >= SFB_MAX_ENTRIES) {
    return;
  }

  Entry = &Menu->Entry[Menu->Count];
  ZeroMem (Entry, sizeof (*Entry));
  Entry->Kind = Kind;
  StrnCpyS (Entry->Desc, SFB_DESC_CHARS, Desc, SFB_DESC_CHARS - 1);
  Menu->Count++;
}

/* ---- text list parsing (BOOTENTRIES / DRIVER.LIST) ---------------------- */

/*
 * Copy one line out of an ASCII buffer into Line, advancing *Cursor past the
 * terminating newline. A trailing '\r' is dropped so CRLF files parse cleanly.
 * Returns FALSE only when the buffer is exhausted; an over-long line is fully
 * consumed, returned as an empty line with *TooLong set, and skipped by callers.
 */
STATIC
BOOLEAN
SfbNextLine (IN OUT CONST CHAR8 **Cursor,
             OUT CHAR8            *Line,
             IN UINTN             LineBytes,
             OUT BOOLEAN          *TooLong)
{
  CONST CHAR8  *Ptr = *Cursor;
  UINTN        Count = 0;

  *TooLong = FALSE;
  if (*Ptr == '\0' || LineBytes == 0) {
    return FALSE;
  }

  while (*Ptr != '\0' && *Ptr != '\n') {
    if (*Ptr == '\r' && (Ptr[1] == '\n' || Ptr[1] == '\0')) {
      /* Drop only the CR in a conventional CRLF line ending. */
    } else {
      if (Count + 1 >= LineBytes) {
        *TooLong = TRUE;
      } else if (!*TooLong) {
        Line[Count++] = *Ptr;
      }
    }
    Ptr++;
  }

  if (*Ptr == '\n') {
    Ptr++;
  }

  if (*TooLong) {
    Count = 0;
  }
  Line[Count] = '\0';
  *Cursor = Ptr;

  return TRUE;
}

/*
 * Turn an ASCII path that is relative to the volume root into an absolute
 * Unicode path on that volume: leading whitespace and one optional separator
 * are stripped, '/' is normalised to '\', and trailing spaces are trimmed.
 * Empty, dotted, double-separator and trailing-separator paths are rejected.
 */
STATIC
BOOLEAN
SfbAsciiRelPathToUnicode (IN CONST CHAR8 *Rel, OUT CHAR16 *Out, IN UINTN OutChars)
{
  UINTN        Count;
  CONST CHAR8  *Scan;
  CONST CHAR8  *End;

  while (*Rel == ' ' || *Rel == '\t') {
    Rel++;
  }
  if (*Rel == '\0' || *Rel == '#') {
    return FALSE;
  }

  /*
   * Preserve the existing root-relative convenience of accepting one leading
   * separator, but reject syntax that would be hidden by normalization below.
   * In particular, double separators and trailing separators must not become
   * a different canonical path before the shared acceptance seam sees them.
   */
  for (Scan = Rel; *Scan != '\0'; Scan++) {
    if (*Scan == '/' || *Scan == '\\') {
      if (Scan != Rel && (Scan[-1] == '/' || Scan[-1] == '\\')) {
        return FALSE;
      }
    }
  }
  End = Rel + AsciiStrLen (Rel);
  while (End > Rel && (End[-1] == ' ' || End[-1] == '\t')) {
    End--;
  }
  if (End > Rel && (End[-1] == '/' || End[-1] == '\\')) {
    return FALSE;
  }

  /* A leading separator would make Open () treat the path as already absolute;
   * the record is specified as root-relative, so drop it and add our own. */
  while (*Rel == '/' || *Rel == '\\') {
    Rel++;
  }

  if (Out == NULL || OutChars < 2) {
    return FALSE;
  }

  Count = 0;
  Out[Count++] = L'\\';

  for (; *Rel != '\0'; Rel++) {
    CHAR8  Ch = *Rel;

    if (Count + 1 >= OutChars) {
      Out[0] = L'\0';
      return FALSE;
    }
    if ((UINT8)Ch < 0x20 || (UINT8)Ch > 0x7e) {
      Out[0] = L'\0';
      return FALSE;
    }
    if (Ch == '/') {
      Ch = '\\';
    }
    Out[Count++] = (CHAR16)Ch;
  }

  while (Count > 1 && (Out[Count - 1] == L' ' || Out[Count - 1] == L'\\')) {
    Count--;
  }
  Out[Count] = L'\0';

  return (BOOLEAN)(Count > 1);
}

/*
 * Parse one BOOTENTRIES line "<name>:<root-relative path>" into a description
 * and an absolute volume path. Returns FALSE for blank/comment lines, a missing
 * separator, an empty name or an empty path.
 *
 * A leading '$' on the name marks a "no default" entry: *NoDefault is set TRUE
 * and the marker is stripped from the returned name, so "$Tools:tools.efi" is
 * displayed as "Tools" but, when launched, never replaces the saved default.
 *
 * A leading '%' instead of a name marks a submenu: *IsSubmenu is set TRUE and
 * the path names another ENTRIES file (same format, paths still relative to the
 * boot root) to open when the row is selected. The '%' and '$' markers are
 * mutually exclusive: a submenu row is never a launch target, so "no default"
 * does not apply to it.
 */
STATIC
BOOLEAN
SfbParseBootEntryLine (IN CONST CHAR8 *Line,
                       OUT CHAR16     *Name,
                       IN UINTN       NameChars,
                       OUT CHAR16     *Path,
                       IN UINTN       PathChars,
                       OUT BOOLEAN    *NoDefault,
                       OUT BOOLEAN    *IsSubmenu)
{
  CONST CHAR8  *Colon = NULL;
  CONST CHAR8  *Ptr;
  UINTN        Count = 0;

  if (NoDefault != NULL) {
    *NoDefault = FALSE;
  }
  if (IsSubmenu != NULL) {
    *IsSubmenu = FALSE;
  }

  while (*Line == ' ' || *Line == '\t') {
    Line++;
  }
  if (*Line == '\0' || *Line == '#') {
    return FALSE;
  }

  if (*Line == '%') {
    if (IsSubmenu != NULL) {
      *IsSubmenu = TRUE;
    }
    Line++;
  } else if (*Line == '$') {
    if (NoDefault != NULL) {
      *NoDefault = TRUE;
    }
    Line++;
  }

  for (Ptr = Line; *Ptr != '\0'; Ptr++) {
    if (*Ptr == ':') {
      Colon = Ptr;
      break;
    }
  }
  if (Colon == NULL) {
    return FALSE;
  }

  for (Ptr = Line; Ptr < Colon && Count + 1 < NameChars; Ptr++) {
    if ((UINT8)*Ptr < 0x20 || (UINT8)*Ptr > 0x7e) {
      continue;
    }
    Name[Count++] = (CHAR16)*Ptr;
  }
  while (Count > 0 && Name[Count - 1] == L' ') {
    Count--;
  }
  Name[Count] = L'\0';
  if (Count == 0) {
    return FALSE;
  }

  return SfbAsciiRelPathToUnicode (Colon + 1, Path, PathChars);
}

/*
 * Build an absolute volume path by prepending RootPrefix to a root-relative
 * suffix that already begins with a backslash. RootPrefix is "" for FAT32, so
 * the suffix passes through untouched; for the ext4 persist volume it is
 * "\efisp", turning "\EFI\BOOT\BOOTAA64.EFI" into "\efisp\EFI\BOOT\BOOTAA64.EFI".
 * The suffix always carries the joining separator, so nothing is inserted
 * between the two halves.
 */
STATIC
EFI_STATUS
SfbJoinRoot (IN CONST CHAR16 *RootPrefix,
             IN CONST CHAR16 *Suffix,
             OUT CHAR16      *Out,
             IN UINTN         OutChars)
{
  RETURN_STATUS  Status;
  UINTN          PrefixLength;
  UINTN          SuffixLength;

  if (RootPrefix == NULL || Suffix == NULL || Out == NULL || OutChars == 0) {
    return EFI_INVALID_PARAMETER;
  }

  PrefixLength = StrLen (RootPrefix);
  SuffixLength = StrLen (Suffix);
  if (PrefixLength >= OutChars ||
      SuffixLength >= OutChars - PrefixLength) {
    return EFI_BUFFER_TOO_SMALL;
  }

  Status = StrnCpyS (Out, OutChars, RootPrefix, OutChars - 1);
  if (RETURN_ERROR (Status)) {
    return (EFI_STATUS)Status;
  }
  Status = StrCatS (Out, OutChars, Suffix);
  return RETURN_ERROR (Status) ? (EFI_STATUS)Status : EFI_SUCCESS;
}

/*
 * Read the ENTRIES file at EntriesPath (an absolute volume path) on Volume and
 * add one menu entry for each line that names a file present on the volume. A
 * '%' line names a submenu and points at another ENTRIES file. Entries already
 * discovered (e.g. the auto-scanned boot loader, or an identical earlier line)
 * are not listed twice. RootPrefix (see SfbVolumeRootPrefix) is "" for FAT32
 * and "\efisp" for the ext4 persist volume, so the same logic serves both; it
 * is prepended to every root-relative path inside the file.
 */
STATIC
VOID
SfbAppendEntriesFile (IN OUT SFB_MENU_STATE *Menu,
                      IN EFI_HANDLE         Volume,
                      IN EFI_FILE_PROTOCOL  *Root,
                      IN CONST CHAR16       *RootPrefix,
                      IN CONST CHAR16       *EntriesPath,
                      IN UINTN              EntryLimit)
{
  CHAR8        *Buffer;
  UINTN        Size = 0;
  CONST CHAR8  *Cursor;
  CHAR8        Line[SFB_PATH_CHARS + SFB_DESC_CHARS + 4];
  BOOLEAN      TooLong;
  EFI_STATUS   JoinStatus;

  Buffer = AllocateZeroPool (SFB_LIST_MAX_BYTES + 1);
  if (Buffer == NULL) {
    return;
  }

  if (EFI_ERROR (SfbReadFileBytes (Root, EntriesPath, Buffer,
                                   SFB_LIST_MAX_BYTES, &Size))) {
    FreePool (Buffer);
    return;
  }
  Buffer[Size] = '\0';

  Cursor = Buffer;
  while (SfbNextLine (&Cursor, Line, sizeof (Line), &TooLong)) {
    CHAR16          Name[SFB_DESC_CHARS];
    CHAR16          RelPath[SFB_PATH_CHARS];
    CHAR16          Path[SFB_PATH_CHARS];
    SFB_BOOT_ENTRY  *Slot;
    UINTN           Index;
    BOOLEAN         Duplicate = FALSE;
    BOOLEAN         NoDefault = FALSE;
    BOOLEAN         IsSubmenu = FALSE;

    if (TooLong) {
      DEBUG ((EFI_D_ERROR, "SFB: ENTRIES line too long; skipped\n"));
      continue;
    }

    if (Menu->Count >= EntryLimit) {
      DEBUG ((EFI_D_ERROR, "SFB: entry list full, ENTRIES truncated\n"));
      break;
    }

    if (!SfbParseBootEntryLine (Line, Name, SFB_DESC_CHARS, RelPath,
                                SFB_PATH_CHARS, &NoDefault, &IsSubmenu)) {
      continue;
    }

    JoinStatus = SfbJoinRoot (RootPrefix, RelPath, Path, SFB_PATH_CHARS);
    if (EFI_ERROR (JoinStatus)) {
      DEBUG ((EFI_D_ERROR, "SFB: ENTRIES path overflow; skipped: %r\n",
              JoinStatus));
      continue;
    }
    if (!SfbIsCanonicalPath (Path)) {
      DEBUG ((EFI_D_ERROR, "SFB: ENTRIES rejected non-canonical path '%s'\n",
              Path));
      continue;
    }

    if (!SfbFileExists (Root, Path)) {
      DEBUG ((EFI_D_INFO, "SFB: ENTRIES '%s' -> '%s' not present\n",
              Name, Path));
      continue;
    }

    Slot = &Menu->Entry[Menu->Count];

    if (IsSubmenu) {
      /* No DevicePath: a submenu row is opened, not launched. The path points
       * at the child ENTRIES file and is resolved relative to the same boot
       * root as everything else in this file. */
      ZeroMem (Slot, sizeof (*Slot));
      Slot->Kind = SfbEntrySubmenu;
      Slot->Volume = Volume;
      StrnCpyS (Slot->Path, SFB_PATH_CHARS, Path, SFB_PATH_CHARS - 1);
      StrnCpyS (Slot->Desc, SFB_DESC_CHARS, Name, SFB_DESC_CHARS - 1);
      DEBUG ((EFI_D_INFO, "SFB: submenu entry '%s' -> '%s'\n", Name, Path));
      Menu->Count++;
      continue;
    }

    if (EFI_ERROR (SfbMakeFileEntry (Volume, Path, Name, Slot))) {
      continue;
    }
    Slot->NoDefault = NoDefault;

    for (Index = 0; Index < Menu->Count; Index++) {
      if (SfbSameDevicePath (Menu->Entry[Index].DevicePath, Slot->DevicePath)) {
        Duplicate = TRUE;
        break;
      }
    }
    if (Duplicate) {
      SfbFreeEntry (Slot);
      continue;
    }

    DEBUG ((EFI_D_INFO, "SFB: ENTRIES entry '%s' -> '%s'\n", Name, Path));
    Menu->Count++;
  }

  FreePool (Buffer);
}

/* Walk every FAT32/ext4 boot volume looking for the well-known boot loader path. */
STATIC
VOID
SfbScanVolumes (IN OUT SFB_MENU_STATE *Menu)
{
  EFI_STATUS         Status;
  EFI_HANDLE         *Volumes = NULL;
  UINTN              VolumeCount = 0;
  UINTN              Index;
  UINT32             NoName = 0;

  Status = SfbLocateVolumes (&Volumes, &VolumeCount);
  if (EFI_ERROR (Status) || Volumes == NULL) {
    DEBUG ((EFI_D_INFO, "SFB: no boot volumes: %r\n", Status));
    return;
  }

  for (Index = 0; Index < VolumeCount; Index++) {
    EFI_FILE_PROTOCOL  *Root = NULL;
    CONST CHAR16       *RootPrefix;
    CHAR16             BootPath[SFB_PATH_CHARS];
    CHAR16             DescPath[SFB_PATH_CHARS];
    CHAR16             BootentriesPath[SFB_PATH_CHARS];
    CHAR16             Desc[SFB_DESC_CHARS];

    if (Menu->Count >= SFB_MAX_ENTRIES) {
      DEBUG ((EFI_D_ERROR, "SFB: entry list full, %u volumes not scanned\n",
              (UINT32)(VolumeCount - Index)));
      break;
    }

    /* RootPrefix is "" for FAT32 and "\efisp" for the ext4 persist volume, so
     * the well-known paths land at the volume root or under \efisp as
     * appropriate. */
    RootPrefix = SfbVolumeRootPrefix (Volumes[Index]);
    Status = SfbJoinRoot (RootPrefix, SFB_BOOT_FILE_PATH, BootPath,
                          SFB_PATH_CHARS);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "SFB: boot paths overflow; volume skipped: %r\n",
              Status));
      continue;
    }
    Status = SfbJoinRoot (RootPrefix, SFB_DESC_FILE_PATH, DescPath,
                          SFB_PATH_CHARS);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "SFB: description path overflow; volume skipped: %r\n",
              Status));
      continue;
    }
    Status = SfbJoinRoot (RootPrefix, SFB_BOOTENTRIES_PATH, BootentriesPath,
                          SFB_PATH_CHARS);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR,
              "SFB: BOOTENTRIES path overflow; volume skipped: %r\n",
              Status));
      continue;
    }

    Status = SfbOpenVolumeRoot (Volumes[Index], &Root);
    if (EFI_ERROR (Status) || Root == NULL) {
      continue;
    }

    /* Entries listed explicitly in <RootPrefix>\BOOTENTRIES come first. */
    SfbAppendEntriesFile (Menu, Volumes[Index], Root, RootPrefix,
                          BootentriesPath, SFB_MAX_ENTRIES);

    /*
     * Then the auto-discovered well-known boot loader, if the volume carries
     * one. <RootPrefix>\EFI\DESC names it; volumes without one are numbered
     * off in the order they were found, so every row still has a label the
     * user can tell apart even when nothing on disk offers one.
     */
    if (Menu->Count < SFB_MAX_ENTRIES &&
        SfbFileExists (Root, BootPath)) {
      SFB_BOOT_ENTRY  *Slot = &Menu->Entry[Menu->Count];

      Desc[0] = L'\0';
      SfbReadAnsiDescription (Root, DescPath, Desc, SFB_DESC_CHARS);
      if (Desc[0] == L'\0') {
        UnicodeSPrint (Desc, sizeof (Desc), L"NONAME%u", NoName++);
      }

      Status = SfbMakeFileEntry (Volumes[Index], BootPath, Desc, Slot);
      if (!EFI_ERROR (Status)) {
        UINTN    Prev;
        BOOLEAN  Duplicate = FALSE;

        /* A BOOTENTRIES line may already point at this same image. */
        for (Prev = 0; Prev < Menu->Count; Prev++) {
          if (SfbSameDevicePath (Menu->Entry[Prev].DevicePath,
                                 Slot->DevicePath)) {
            Duplicate = TRUE;
            break;
          }
        }

        if (Duplicate) {
          SfbFreeEntry (Slot);
        } else {
          DEBUG ((EFI_D_INFO, "SFB: boot entry '%s' on volume %u\n",
                  Desc, (UINT32)Index));
          Menu->Count++;
        }
      }
    }

    Root->Close (Root);
  }

  FreePool (Volumes);
}

STATIC
VOID
SfbAppendCustomEntry (IN OUT SFB_MENU_STATE *Menu)
{
  SFB_BOOT_ENTRY  Custom;
  UINTN           Index;

  if (EFI_ERROR (SfbLoadEntryRecord (SFB_STORE_CUSTOM, &Custom))) {
    return;
  }

  /* Do not list it twice if scanning already turned up the same image. */
  for (Index = 0; Index < Menu->Count; Index++) {
    if (SfbSameDevicePath (Menu->Entry[Index].DevicePath, Custom.DevicePath)) {
      SfbFreeEntry (&Custom);
      return;
    }
  }

  if (Menu->Count >= SFB_MAX_ENTRIES) {
    SfbFreeEntry (&Custom);
    return;
  }

  Custom.IsCustom = TRUE;
  if (Custom.Desc[0] == L'\0') {
    StrnCpyS (Custom.Desc, SFB_DESC_CHARS, L"Custom entry", SFB_DESC_CHARS - 1);
  }

  CopyMem (&Menu->Entry[Menu->Count], &Custom, sizeof (Custom));
  Menu->Count++;
}

STATIC
VOID
SfbResolveDefault (IN OUT SFB_MENU_STATE *Menu)
{
  SFB_BOOT_ENTRY  Saved;
  UINTN           Index;

  Menu->DefaultIndex = SFB_NO_INDEX;
  Menu->DefaultIsPersisted = FALSE;

  if (!EFI_ERROR (SfbLoadEntryRecord (SFB_STORE_DEFAULT, &Saved))) {
    for (Index = 0; Index < Menu->Count; Index++) {
      if (SfbSameDevicePath (Menu->Entry[Index].DevicePath, Saved.DevicePath)) {
        Menu->DefaultIndex = Index;
        Menu->DefaultIsPersisted = TRUE;
        break;
      }
    }
    SfbFreeEntry (&Saved);
  }
  if (Menu->DefaultIndex == SFB_NO_INDEX) {
    for (Index = 0; Index < Menu->Count; Index++) {
      if (Menu->Entry[Index].Kind == SfbEntryEfiFile) {
        Menu->DefaultIndex = Index;
        break;
      }
    }
  }
}

STATIC CONST CHAR16 *
SfbModeLabel (IN SFB_BOOT_MODE Mode)
{
  switch (Mode) {
    case SfbBootModeHonestUnlocked:
      return L"Boot Mode: Mode 0 - Honest unlocked";
    case SfbBootModeAblFakeLocked:
      return L"Boot Mode: Mode 1 - ABL fake locked";
    case SfbBootModeKmProfile:
      return L"Boot Mode: Mode 2 - KM/SPSS profile spoof";
    default:
      return L"Boot Mode: Mode 1 - ABL fake locked";
  }
}

VOID
SfbBuildMenu (OUT SFB_MENU_STATE *Menu, IN SFB_BOOT_MODE Mode)
{
  ZeroMem (Menu, sizeof (*Menu));
  Menu->Mode = Mode;
  Menu->DefaultIndex = SFB_NO_INDEX;

  /* Keep the policy selector as the first row, before discovered entries. */
  SfbAppendBuiltIn (Menu, SfbEntryMode, SfbModeLabel (Mode));
  SfbScanVolumes (Menu);
  /*
   * Preserve one slot for a saved custom entry and four for the mandatory
   * recovery actions below. Discovery is best-effort; fastboot, selector,
   * power-off, and restart must never disappear when BOOTENTRIES is full.
   */
  while (Menu->Count >= SFB_MAX_ENTRIES - 4) {
    Menu->Count--;
    SfbFreeEntry (&Menu->Entry[Menu->Count]);
  }
  SfbAppendCustomEntry (Menu);

  SfbAppendBuiltIn (Menu, SfbEntryFastboot, L"Enter Fastboot");
  SfbAppendBuiltIn (Menu, SfbEntrySelector, L"Enter EFI Program Selector");
  SfbAppendBuiltIn (Menu, SfbEntryPowerOff, L"Power Off");
  SfbAppendBuiltIn (Menu, SfbEntryRestart, L"Restart");

  SfbResolveDefault (Menu);
}

VOID
SfbFreeMenu (IN OUT SFB_MENU_STATE *Menu)
{
  UINTN Index;

  for (Index = 0; Index < Menu->Count; Index++) {
    SfbFreeEntry (&Menu->Entry[Index]);
  }
  Menu->Count = 0;
  Menu->DefaultIndex = SFB_NO_INDEX;
}

EFI_STATUS
SfbBuildSubMenu (OUT SFB_MENU_STATE *Menu,
                 IN EFI_HANDLE      Volume,
                 IN CONST CHAR16    *EntriesPath,
                 IN SFB_BOOT_MODE    Mode)
{
  EFI_FILE_PROTOCOL  *Root = NULL;
  CONST CHAR16       *RootPrefix;

  ZeroMem (Menu, sizeof (*Menu));
  Menu->Mode = Mode;
  Menu->DefaultIndex = SFB_NO_INDEX;

  if (Volume == NULL || EntriesPath == NULL || EntriesPath[0] == L'\0') {
    return EFI_INVALID_PARAMETER;
  }

  /* The submenu shares the parent volume's boot root: every path inside the
   * ENTRIES file is resolved relative to it, never to the file's own directory,
   * so the same RootPrefix that served the parent menu serves the child. */
  RootPrefix = SfbVolumeRootPrefix (Volume);
  if (!EFI_ERROR (SfbOpenVolumeRoot (Volume, &Root)) && Root != NULL) {
    SfbAppendEntriesFile (Menu, Volume, Root, RootPrefix, EntriesPath,
                          SFB_MAX_ENTRIES - 1);
    Root->Close (Root);
  }

  /* Always offer a way out: an empty or unreadable file still leaves the user
   * on a screen with a Back row. */
  SfbAppendBuiltIn (Menu, SfbEntryBack, L"Back");
  return EFI_SUCCESS;
}

/* ---- launching ---------------------------------------------------------- */

/*
 * On this platform the firmware's LoadImage refuses images that come off a
 * FAT32 volume: the verified-boot policy behind the Security Arch protocols is
 * built for the signed boot chain, not for the arbitrary loaders this menu
 * exists to run. The device is unlocked and the user has asked for these images
 * explicitly, so the authentication hooks are neutralised for the duration of
 * the load and put back immediately afterwards.
 *
 * This patches the live protocol function pointers rather than reinstalling the
 * protocol, so it works no matter which driver produced it and touches nothing
 * else in the system.
 */

BOOLEAN
SfbIsManagedAblEntry (IN CONST SFB_BOOT_ENTRY *Entry)
{
  if (Entry == NULL || Entry->Kind != SfbEntryEfiFile) {
    return FALSE;
  }
  return SfbIsManagedAblPath (Entry->Path);
}



EFI_STATUS
SfbLoadDriver (IN EFI_HANDLE Volume, IN CONST CHAR16 *Path)
{
  EFI_STATUS                Status;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  EFI_HANDLE                ImageHandle = NULL;
  if (Volume == NULL || !SfbIsCanonicalPath (Path)) {
    SfbDisarmManagedAblHooks ();
    return EFI_INVALID_PARAMETER;
  }

  DevicePath = FileDevicePath (Volume, Path);
  if (DevicePath == NULL) {
    SfbDisarmManagedAblHooks ();
    return EFI_OUT_OF_RESOURCES;
  }

  /* Same verified-boot bypass the entry launch relies on; see below. */
  SfbBypassSecurity ();
  /* A returned child must never leave managed policy active for a driver. */
  SfbDisarmManagedAblHooks ();
  Status = gBS->LoadImage (FALSE, gImageHandle, DevicePath, NULL, 0,
                           &ImageHandle);
  SfbRestoreSecurity ();
  FreePool (DevicePath);

  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: driver LoadImage '%s' failed: %r\n",
            Path, Status));
    SfbDisarmManagedAblHooks ();
    return Status;
  }

  /* A UEFI driver installs its driver binding here and returns; the caller runs
   * the connect pass. A driver that returns an error is unloaded by the core. */
  Status = gBS->StartImage (ImageHandle, NULL, NULL);
  SfbDisarmManagedAblHooks ();
  DEBUG ((EFI_D_INFO, "SFB: driver '%s' start: %r\n", Path, Status));

  return Status;
}

/* Copy Path's parent directory into Out. A path with no directory component
 * (a file at the volume root) yields "\". */
STATIC
EFI_STATUS
SfbDirOf (IN CONST CHAR16 *Path, OUT CHAR16 *Out, IN UINTN OutChars)
{
  UINTN         Index;
  UINTN         LastSep = 0;
  RETURN_STATUS CopyStatus;

  if (Path == NULL || Out == NULL || OutChars == 0) {
    return EFI_INVALID_PARAMETER;
  }
  if (OutChars < 2 || StrLen (Path) >= OutChars) {
    return EFI_BUFFER_TOO_SMALL;
  }

  CopyStatus = StrnCpyS (Out, OutChars, Path, OutChars - 1);
  if (RETURN_ERROR (CopyStatus)) {
    return (EFI_STATUS)CopyStatus;
  }

  for (Index = 0; Out[Index] != L'\0'; Index++) {
    if (Out[Index] == L'\\') {
      LastSep = Index;
    }
  }

  if (LastSep == 0) {
    Out[0] = L'\\';
    Out[1] = L'\0';
  } else {
    Out[LastSep] = L'\0';
  }

  return EFI_SUCCESS;
}

/* Append Child to a directory path, inserting a separator unless the directory
 * is the root. */
STATIC
EFI_STATUS
SfbJoinChild (IN OUT CHAR16    *Path,
              IN UINTN          OutChars,
              IN CONST CHAR16  *Child)
{
  RETURN_STATUS  Status;
  UINTN          PathLength;
  UINTN          ChildLength;

  if (Path == NULL || Child == NULL || OutChars == 0) {
    return EFI_INVALID_PARAMETER;
  }

  PathLength = StrLen (Path);
  ChildLength = StrLen (Child);
  if (PathLength >= OutChars) {
    return EFI_BUFFER_TOO_SMALL;
  }

  if (SfbIsRootPath (Path)) {
    if (ChildLength >= OutChars - PathLength) {
      return EFI_BUFFER_TOO_SMALL;
    }
  } else if (PathLength + 1 >= OutChars ||
             ChildLength >= OutChars - PathLength - 1) {
    return EFI_BUFFER_TOO_SMALL;
  }

  if (!SfbIsRootPath (Path)) {
    Status = StrCatS (Path, OutChars, L"\\");
    if (RETURN_ERROR (Status)) {
      return (EFI_STATUS)Status;
    }
  }
  Status = StrCatS (Path, OutChars, Child);
  return RETURN_ERROR (Status) ? (EFI_STATUS)Status : EFI_SUCCESS;
}

/*
 * Load the drivers named in the DRIVER.LIST file sitting in EntryPath's own
 * directory, if that file exists, then connect controllers so the drivers bind.
 * Each line is a driver path relative to the volume root. Missing list or
 * missing drivers are not fatal: the entry still launches.
 */
STATIC
VOID
SfbPreloadDrivers (IN EFI_HANDLE Volume, IN CONST CHAR16 *EntryPath)
{
  EFI_FILE_PROTOCOL  *Root = NULL;
  EFI_STATUS          Status;
  CHAR16              ListPath[SFB_PATH_CHARS];
  CHAR8               *Buffer;
  UINTN               Size = 0;
  CONST CHAR8         *Cursor;
  CHAR8               Line[SFB_PATH_CHARS];
  BOOLEAN             LoadedAny = FALSE;
  BOOLEAN             TooLong;

  if (Volume == NULL || EntryPath == NULL) {
    return;
  }

  Status = SfbDirOf (EntryPath, ListPath, SFB_PATH_CHARS);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_WARN,
            "SFB: DRIVER.LIST path overflow for entry '%s': %r\n",
            EntryPath, Status));
    return;
  }
  Status = SfbJoinChild (ListPath, SFB_PATH_CHARS, SFB_DRIVER_LIST_NAME);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_WARN,
            "SFB: DRIVER.LIST path overflow for entry '%s': %r\n",
            EntryPath, Status));
    return;
  }

  if (EFI_ERROR (SfbOpenVolumeRoot (Volume, &Root)) || Root == NULL) {
    return;
  }

  Buffer = AllocateZeroPool (SFB_LIST_MAX_BYTES + 1);
  if (Buffer == NULL) {
    Root->Close (Root);
    return;
  }

  if (EFI_ERROR (SfbReadFileBytes (Root, ListPath, Buffer, SFB_LIST_MAX_BYTES,
                                   &Size))) {
    /* No DRIVER.LIST beside the entry: nothing to preload. */
    FreePool (Buffer);
    Root->Close (Root);
    return;
  }
  Buffer[Size] = '\0';
  Root->Close (Root);

  DEBUG ((EFI_D_INFO, "SFB: DRIVER.LIST '%s' for entry '%s'\n",
          ListPath, EntryPath));

  Cursor = Buffer;
  while (SfbNextLine (&Cursor, Line, sizeof (Line), &TooLong)) {
    CHAR16  RelPath[SFB_PATH_CHARS];
    CHAR16  DriverPath[SFB_PATH_CHARS];

    if (TooLong) {
      DEBUG ((EFI_D_ERROR, "SFB: DRIVER.LIST line too long; skipped\n"));
      continue;
    }

    if (!SfbAsciiRelPathToUnicode (Line, RelPath, SFB_PATH_CHARS)) {
      continue;
    }
    /* Driver paths are relative to the same virtual root as the entry itself
     * (the volume root for FAT32, \efisp for the ext4 persist volume). */
    Status = SfbJoinRoot (SfbVolumeRootPrefix (Volume), RelPath, DriverPath,
                          SFB_PATH_CHARS);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_WARN, "SFB: DRIVER.LIST path overflow; skipped: %r\n",
              Status));
      continue;
    }
    if (!EFI_ERROR (SfbLoadDriver (Volume, DriverPath))) {
      LoadedAny = TRUE;
    }
  }

  FreePool (Buffer);

  if (LoadedAny) {
    SfbConnectAll ();
  }
}

EFI_STATUS
SfbLaunchEntry (IN CONST SFB_BOOT_ENTRY *Entry,
                IN BOOLEAN              Temporary,
                IN BOOLEAN              ClearScreen,
                IN SFB_BOOT_MODE        Mode)
{
  EFI_STATUS Status;
  BOOLEAN Managed;
  SFB_BOOT_MODE EffectiveMode = Mode;
  SFB_MODE2_PROFILE Profile;
  SFB_TZ_MAP TzMap;
  CONST SFB_TZ_MAP *TzMapPtr = NULL;
  /* Returned children and failed launches must never leave a policy wrapper
   * active while DRIVER.LIST images are loaded. */
  SfbDisarmManagedAblHooks ();

  if (Entry == NULL || Entry->Kind != SfbEntryEfiFile ||
      Entry->DevicePath == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Managed = SfbIsManagedAblEntry (Entry);

  if (Managed) {
    Status = SfbLoadTzMap (Entry, &TzMap);
    if (EFI_ERROR (Status)) {
      SfbTzMapBuiltinDefault (&TzMap);
      DEBUG ((EFI_D_WARN,
              "SFB: MARK tzmap-load status=%r fallback=builtin\n",
              Status));
    } else {
      DEBUG ((EFI_D_INFO,
              "SFB: MARK tzmap-load status=%r commands=%u flags=0x%08x\n",
              Status, (UINTN)TzMap.CommandCount, (UINTN)TzMap.Flags));
    }
    TzMapPtr = &TzMap;
  }

  SfbShowBootingScreen (Entry->Desc, Entry->Path, ClearScreen);
  if (!Temporary && !Entry->NoDefault) {
    SfbSaveDefaultEntry (Entry);
  }

  if (Managed && Mode == SfbBootModeKmProfile) {
    EFI_STATUS ProfileStatus;

    Status = SfbResolveManagedAblMode (Entry, Mode, &EffectiveMode,
                                       &Profile, &ProfileStatus);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    if (EFI_ERROR (ProfileStatus)) {
      Print (L"SFB: invalid Mode 2 profile beside '%s'; launching Mode 0 (%r)\n",
             Entry->Path, ProfileStatus);
    }
  }
  DEBUG ((EFI_D_INFO,
          "SFB: MARK launch managed=%u requested-mode=%u "
          "effective-mode=%u temporary=%u\n",
          (UINT32)Managed, (UINT32)Mode, (UINT32)EffectiveMode,
          (UINT32)Temporary));

  SfbPreloadDrivers (Entry->Volume, Entry->Path);
  SfbBypassSecurity ();

  Status = SfbLaunchImage (
             Entry->DevicePath,
             Managed,
             EffectiveMode,
             EffectiveMode == SfbBootModeKmProfile ? &Profile : NULL,
             TzMapPtr);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: '%s' failed or returned: %r\n",
            Entry->Path, Status));
  }
  return Status;
}

BOOLEAN
SfbLaunchDefaultEntry (IN SFB_BOOT_MODE Mode)
{
  SFB_MENU_STATE Menu;
  BOOLEAN HasDefault;

  SfbBuildMenu (&Menu, Mode);
  HasDefault = (BOOLEAN)(Menu.DefaultIsPersisted &&
                         Menu.DefaultIndex != SFB_NO_INDEX);
  if (HasDefault) {
    DEBUG ((EFI_D_INFO, "SFB: launching default entry '%s'\n",
            Menu.Entry[Menu.DefaultIndex].Desc));
    SfbLaunchEntry (&Menu.Entry[Menu.DefaultIndex], FALSE, FALSE, Mode);
  }
  SfbFreeMenu (&Menu);
  return HasDefault;
}