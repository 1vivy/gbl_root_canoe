/*
 * Boot entry list and launching for the super-fastboot boot menu.
 *
 * The menu is a reader: all state comes from canoe.cfg on the boot root, and
 * this module never writes a file or block device.
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
#include "SuperFbBls.h"
#include "SuperFbLinuxBoot.h"
#include "SuperFbSlots.h"

/* Keeps the translation unit legal when the feature is compiled out. */
CONST CHAR8 *gSfbEntriesModuleTag = "SuperFbEntries";
STATIC
EFI_STATUS
SfbJoinRoot (IN CONST CHAR16 *RootPrefix,
             IN CONST CHAR16 *Suffix,
             OUT CHAR16      *Out,
             IN UINTN         OutChars);

STATIC
VOID
SfbAsciiToUnicode (IN CONST CHAR8 *Ascii, OUT CHAR16 *Unicode, IN UINTN Chars)
{
  UINTN Index;

  if (Unicode == NULL || Chars == 0) {
    return;
  }
  for (Index = 0; Index + 1 < Chars && Ascii != NULL && Ascii[Index] != '\0';
       Index++) {
    Unicode[Index] = (CHAR16)(UINT8)Ascii[Index];
  }
  Unicode[Index] = L'\0';
}

EFI_STATUS
SfbLoadBootConfig (OUT SFB_CONFIG *Config, OUT EFI_HANDLE *Volume)
{
  EFI_STATUS Status;
  EFI_HANDLE *Volumes = NULL;
  UINTN VolumeCount = 0;
  UINTN Index;

  if (Config == NULL || Volume == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (Config, sizeof (*Config));
  *Volume = NULL;

  Status = SfbLocateVolumes (&Volumes, &VolumeCount);
  if (EFI_ERROR (Status) || Volumes == NULL) {
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < VolumeCount; Index++) {
    EFI_FILE_PROTOCOL *Root = NULL;
    CHAR16 ConfigPath[SFB_PATH_CHARS];
    CHAR8 *Buffer;
    UINTN BytesRead = 0;

    Status = SfbJoinRoot (SfbVolumeRootPrefix (Volumes[Index]),
                          SFB_CONFIG_FILE_PATH, ConfigPath,
                          ARRAY_SIZE (ConfigPath));
    if (EFI_ERROR (Status) ||
        EFI_ERROR (SfbOpenVolumeRoot (Volumes[Index], &Root)) ||
        Root == NULL) {
      continue;
    }
    if (!SfbFileExists (Root, ConfigPath)) {
      Root->Close (Root);
      continue;
    }

    Buffer = AllocateZeroPool (SFB_LIST_MAX_BYTES + 1);
    if (Buffer == NULL) {
      Root->Close (Root);
      *Volume = Volumes[Index];
      FreePool (Volumes);
      return EFI_OUT_OF_RESOURCES;
    }
    Status = SfbReadFileBytes (Root, ConfigPath, Buffer, SFB_LIST_MAX_BYTES,
                               &BytesRead);
    Root->Close (Root);
    if (EFI_ERROR (Status)) {
      FreePool (Buffer);
      *Volume = Volumes[Index];
      FreePool (Volumes);
      return Status;
    }
    *Volume = Volumes[Index];
    Status = SfbConfigParse (Buffer, BytesRead, Config)
             ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
    DEBUG ((EFI_D_INFO, "SFB: canoe.cfg volume=%u status=%r\n",
            (UINT32)Index, Status));
    FreePool (Buffer);
    FreePool (Volumes);
    return Status;
  }

  FreePool (Volumes);
  return EFI_NOT_FOUND;
}

/*
 * A missing or unreachable root is first-run too: when nothing can be
 * launched, fastboot is the only useful destination for installation.
 */
BOOLEAN
SfbBootRootIsEmpty (VOID)
{
  EFI_STATUS Status;
  EFI_HANDLE *Volumes = NULL;
  UINTN VolumeCount = 0;
  UINTN Index;
  BOOLEAN FoundRoot = FALSE;

  Status = SfbLocateVolumes (&Volumes, &VolumeCount);
  if (EFI_ERROR (Status) || Volumes == NULL || VolumeCount == 0) {
    DEBUG ((EFI_D_WARN,
            "SFB: MARK boot-root reason=no-volumes status=%r\n", Status));
    return TRUE;
  }

  for (Index = 0; Index < VolumeCount; Index++) {
    EFI_FILE_PROTOCOL *Root = NULL;
    CHAR16 ConfigPath[SFB_PATH_CHARS];
    CHAR16 ManagedPath[SFB_PATH_CHARS];

    if (EFI_ERROR (SfbOpenVolumeRoot (Volumes[Index], &Root)) ||
        Root == NULL) {
      continue;
    }
    FoundRoot = TRUE;
    if (!EFI_ERROR (SfbJoinRoot (SfbVolumeRootPrefix (Volumes[Index]),
                                 SFB_CONFIG_FILE_PATH, ConfigPath,
                                 ARRAY_SIZE (ConfigPath))) &&
        SfbFileExists (Root, ConfigPath)) {
      DEBUG ((EFI_D_INFO, "SFB: MARK boot-root reason=populated\n"));
      Root->Close (Root);
      FreePool (Volumes);
      return FALSE;
    }
    if (!EFI_ERROR (SfbJoinRoot (SfbVolumeRootPrefix (Volumes[Index]),
                                 SFB_MANAGED_BOOT_NAME, ManagedPath,
                                 ARRAY_SIZE (ManagedPath))) &&
        SfbFileExists (Root, ManagedPath)) {
      DEBUG ((EFI_D_INFO, "SFB: MARK boot-root reason=populated\n"));
      Root->Close (Root);
      FreePool (Volumes);
      return FALSE;
    }
    Root->Close (Root);
  }

  FreePool (Volumes);
  if (!FoundRoot) {
    DEBUG ((EFI_D_WARN, "SFB: MARK boot-root reason=no-root\n"));
    return TRUE;
  }
  DEBUG ((EFI_D_INFO, "SFB: MARK boot-root reason=empty-root\n"));
  return TRUE;
}

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
  Entry->IsUsb = SfbIsUsbVolume (Volume);
  Entry->BlsIndex = SFB_NO_BLS;
  StrnCpyS (Entry->Path, SFB_PATH_CHARS, PathOnVolume, SFB_PATH_CHARS - 1);
  StrnCpyS (Entry->Desc, SFB_DESC_CHARS, Desc, SFB_DESC_CHARS - 1);

  /*
   * Decided here rather than at each caller, because every entry in the tree
   * is built through this one seam: a config row, a boot-root probe row, a
   * discovered removable loader and a browsed file are all in the same
   * position. Only a managed name ever gets a wrapper, so for anything else
   * the entry's Mode decides nothing and the menu must say so.
   */
  Entry->Passthrough =
    (BOOLEAN)(Entry->IsUsb || !SfbIsManagedAblPath (PathOnVolume));

  /* Capture the volume label while the volume root is already available. */
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
  Entry->BlsIndex = SFB_NO_BLS;
  StrnCpyS (Entry->Desc, SFB_DESC_CHARS, Desc, SFB_DESC_CHARS - 1);
  Menu->Count++;
}

/* ---- text list parsing (DRIVER.LIST) ------------------------------------ */

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
 * Offer the managed loader names the installers write, for a boot root that
 * holds one but no canoe.cfg.
 *
 * That combination is what a dd-only upgrade leaves behind: a new BDS written
 * straight to efisp, the installer never run, so the config it would have
 * authored is absent while boot.efi is sitting right there. Without this probe
 * such a device would come up to a menu offering nothing to boot.
 *
 * The titles match what the installers put in canoe.cfg, so the row does not
 * change its name the first time a config is written.
 */
STATIC
VOID
SfbAppendBootRootEntries (IN OUT SFB_MENU_STATE *Menu)
{
  STATIC CONST struct {
    CONST CHAR16  *Name;
    CONST CHAR16  *Title;
  } Known[] = {
    { SFB_MANAGED_BOOT_NAME,   L"Android" },
    { SFB_MANAGED_BACKUP_NAME, L"Android (previous)" }
  };

  EFI_STATUS  Status;
  EFI_HANDLE  *Volumes = NULL;
  UINTN       VolumeCount = 0;
  UINTN       Index;
  UINTN       Which;

  Status = SfbLocateVolumes (&Volumes, &VolumeCount);
  if (EFI_ERROR (Status) || Volumes == NULL) {
    return;
  }

  for (Index = 0; Index < VolumeCount; Index++) {
    EFI_FILE_PROTOCOL  *Root = NULL;

    if (EFI_ERROR (SfbOpenVolumeRoot (Volumes[Index], &Root)) ||
        Root == NULL) {
      continue;
    }

    for (Which = 0; Which < ARRAY_SIZE (Known); Which++) {
      CHAR16          Path[SFB_PATH_CHARS];
      SFB_BOOT_ENTRY  *Slot;

      if (Menu->Count >= SFB_MAX_ENTRIES) {
        break;
      }
      if (EFI_ERROR (SfbJoinRoot (SfbVolumeRootPrefix (Volumes[Index]),
                                  Known[Which].Name, Path,
                                  ARRAY_SIZE (Path))) ||
          !SfbFileExists (Root, Path)) {
        continue;
      }

      Slot = &Menu->Entry[Menu->Count];
      if (EFI_ERROR (SfbMakeFileEntry (Volumes[Index], Path,
                                       Known[Which].Title, Slot))) {
        continue;
      }
      DEBUG ((EFI_D_INFO, "SFB: boot root entry '%s'\n", Path));
      Menu->Count++;
    }

    Root->Close (Root);
  }

  FreePool (Volumes);
}
/*
 * Where the Boot Loader Specification puts Type #1 entries. One file per
 * entry, and this loader reads them straight off the medium rather than
 * caching an index, so adding a file and rebooting is the whole workflow.
 */
#define SFB_BLS_DIR_PATH  L"\\loader\\entries"

/*
 * Out-of-line payloads for the boot-spec rows: command line, initrd path and
 * DTB path.
 *
 * They are deliberately not fields of SFB_BOOT_ENTRY. That struct is already
 * about 1.2 KB and SFB_MENU_STATE embeds SFB_MAX_ENTRIES of them by value, so
 * another ~900 bytes each would add tens of kilobytes to a stack-allocated
 * menu. The entry carries one byte of index instead.
 *
 * A canoe.cfg row with `options` borrows the same table for its LoadOptions,
 * keyed the same way. It stays an SfbEntryEfiFile - a plain application
 * launch that carries arguments, not a boot-spec row - so the launch path
 * distinguishes the two by Kind and reads the payload by BlsIndex.
 */
#if SFB_CONFIG_OPTIONS_CHARS > SFB_BLS_CMDLINE_CHARS
#error "canoe.cfg options must fit the boot-spec payload it is stored in"
#endif
STATIC SFB_BLS_ENTRY  mSfbBlsPayload[SFB_MAX_ENTRIES];

STATIC
CONST SFB_BLS_ENTRY *
SfbBlsPayload (IN UINT8 Index)
{
  if (Index >= SFB_MAX_ENTRIES) {
    return NULL;
  }
  return &mSfbBlsPayload[Index];
}

/* TRUE when Name ends in ".conf", case-insensitively. */
STATIC
BOOLEAN
SfbBlsIsConfName (IN CONST CHAR16 *Name)
{
  UINTN         Length = StrLen (Name);
  CONST CHAR16  *Tail;
  UINTN         Index;
  STATIC CONST CHAR16  Want[] = L".conf";

  if (Length < 6) {
    /* Shorter than one character plus the suffix: there is no stem to name a
     * row with, so the file is not an entry. */
    return FALSE;
  }
  Tail = Name + (Length - 5);
  for (Index = 0; Index < 5; Index++) {
    CHAR16  Left = Tail[Index];

    if (Left >= L'A' && Left <= L'Z') {
      Left = (CHAR16)(Left + (L'a' - L'A'));
    }
    if (Left != Want[Index]) {
      return FALSE;
    }
  }
  return TRUE;
}

/*
 * Read /loader/entries off one removable volume and append a row per usable
 * entry.
 *
 * Removable-only by design: an internal ESP must not be able to inject rows
 * into the on-device boot root's menu, which is canoe.cfg's territory.
 */
STATIC
VOID
SfbScanBlsEntries (IN OUT SFB_MENU_STATE *Menu,
                   IN EFI_HANDLE          Volume,
                   IN EFI_FILE_PROTOCOL  *Root)
{
  EFI_FILE_PROTOCOL  *Dir = NULL;
  SFB_DIR_ENTRY      *List = NULL;
  CHAR8              *Bytes = NULL;
  UINTN              Count = 0;
  BOOLEAN            Truncated = FALSE;
  UINTN              Index;

  if (EFI_ERROR (Root->Open (Root, &Dir, (CHAR16 *)SFB_BLS_DIR_PATH,
                             EFI_FILE_MODE_READ, 0)) ||
      Dir == NULL) {
    return;
  }

  /* Both buffers are tens of kilobytes between them and this runs inside a
   * menu build whose frame already holds an SFB_MENU_STATE, so neither goes
   * on the stack. */
  List = AllocateZeroPool (SFB_MAX_DIR_ENTRIES * sizeof (SFB_DIR_ENTRY));
  Bytes = AllocateZeroPool (SFB_BLS_MAX_BYTES + 1);
  if (List == NULL || Bytes == NULL) {
    goto Done;
  }

  if (EFI_ERROR (SfbReadDirectory (Dir, List, SFB_MAX_DIR_ENTRIES, &Count,
                                   &Truncated))) {
    goto Done;
  }
  if (Truncated) {
    DEBUG ((EFI_D_WARN, "SFB: MARK bls-truncated dir='%s'\n",
            SFB_BLS_DIR_PATH));
  }

  for (Index = 0; Index < Count && Menu->Count < SFB_MAX_ENTRIES; Index++) {
    CHAR16          Path[SFB_PATH_CHARS];
    CHAR16          Desc[SFB_DESC_CHARS];
    CHAR16          Image[SFB_PATH_CHARS];
    SFB_BLS_ENTRY   Parsed;
    SFB_BOOT_ENTRY  *Slot;
    UINTN           Size = 0;

    if (List[Index].IsDir || !SfbBlsIsConfName (List[Index].Name)) {
      continue;
    }
    if (EFI_ERROR (UnicodeSPrint (Path, sizeof (Path), L"%s\\%s",
                                  SFB_BLS_DIR_PATH, List[Index].Name)) ||
        StrLen (Path) >= SFB_PATH_CHARS) {
      continue;
    }
    if (EFI_ERROR (SfbReadFileBytes (Root, Path, Bytes, SFB_BLS_MAX_BYTES,
                                     &Size))) {
      continue;
    }
    if (!SfbBlsParse (Bytes, Size, &Parsed)) {
      DEBUG ((EFI_D_WARN, "SFB: MARK bls-reject file='%s'\n",
              List[Index].Name));
      continue;
    }

    SfbAsciiToUnicode (Parsed.Image, Image, ARRAY_SIZE (Image));
    if (!SfbFileExists (Root, Image)) {
      DEBUG ((EFI_D_WARN, "SFB: MARK bls-missing file='%s' image='%s'\n",
              List[Index].Name, Image));
      continue;
    }

    if (Parsed.Title[0] != '\0') {
      SfbAsciiToUnicode (Parsed.Title, Desc, ARRAY_SIZE (Desc));
    } else {
      /* No `title`: the file stem is the only other name the author gave us,
       * and an unlabelled row is worse than a filename. */
      StrnCpyS (Desc, SFB_DESC_CHARS, List[Index].Name,
                MIN (SFB_DESC_CHARS - 1, StrLen (List[Index].Name) - 5));
    }

    Slot = &Menu->Entry[Menu->Count];
    if (EFI_ERROR (SfbMakeFileEntry (Volume, Image, Desc, Slot))) {
      continue;
    }

    /*
     * Deliberately not deduplicated against earlier rows by device path. The
     * volume scan does that because two probes of one medium really are the
     * same launch, but a Type #1 entry is distinguished by its payload, not
     * its image: `bootctl` and `kernel-install` routinely write several
     * `.conf` files that name one kernel and differ only in `options`, and a
     * rescue entry collapsing into the normal one would be a silent loss of
     * exactly the entry the operator went looking for.
     */

    Slot->Kind = (Parsed.Kind == SfbBlsKindLinux) ? SfbEntryBlsLinux
                                                  : SfbEntryBlsEfi;
    Slot->BlsIndex = (UINT8)Menu->Count;
    CopyMem (&mSfbBlsPayload[Menu->Count], &Parsed, sizeof (Parsed));

    DEBUG ((EFI_D_INFO,
            "SFB: MARK bls-entry file='%s' kind=%u image='%s' rejected=%u\n",
            List[Index].Name, (UINT32)Parsed.Kind, Image,
            (UINT32)Parsed.RejectedLines));
    Menu->Count++;
  }

Done:
  if (Bytes != NULL) {
    FreePool (Bytes);
  }
  if (List != NULL) {
    FreePool (List);
  }
  Dir->Close (Dir);
}


/*
 * Discover the well-known boot loader on every removable/ESP volume.
 *
 * This is what the loader carries its own FAT stack for: enumerating loaders on
 * media whose firmware exposes nothing but Block I/O. It is additive and runs
 * on every boot, which draws the line exactly - canoe.cfg owns the persist boot
 * root, discovery owns removable media.
 *
 * The ext4 persist volume is therefore skipped. Without that gate this scan
 * would re-find the boot root's own boot.efi and list every configured entry a
 * second time. SfbVolumeIsExt4 reads the cached classification rather than
 * re-probing the block device, which matters here because the scan visits every
 * volume; the two near-identical predicates beside it in the header do re-probe.
 */
STATIC
VOID
SfbScanRemovableVolumes (IN OUT SFB_MENU_STATE *Menu)
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Volumes = NULL;
  UINTN       VolumeCount = 0;
  UINTN       Index;
  UINT32      NoName = 0;

  SfbBootMark (L"scan:locate");
  Status = SfbLocateVolumes (&Volumes, &VolumeCount);
  if (EFI_ERROR (Status) || Volumes == NULL) {
    DEBUG ((EFI_D_INFO, "SFB: no boot volumes: %r\n", Status));
    return;
  }

  for (Index = 0; Index < VolumeCount; Index++) {
    EFI_FILE_PROTOCOL  *Root = NULL;
    SFB_BOOT_ENTRY     *Slot;
    CHAR16             Desc[SFB_DESC_CHARS];
    UINTN              Prev;
    BOOLEAN            Duplicate = FALSE;

    if (Menu->Count >= SFB_MAX_ENTRIES) {
      DEBUG ((EFI_D_ERROR, "SFB: entry list full, %u volumes not scanned\n",
              (UINT32)(VolumeCount - Index)));
      break;
    }

    if (SfbVolumeIsExt4 (Volumes[Index])) {
      continue;
    }

    if (EFI_ERROR (SfbOpenVolumeRoot (Volumes[Index], &Root)) ||
        Root == NULL) {
      continue;
    }

    /*
     * Every volume that reaches here is FAT32, whose boot root is its own
     * root, so the well-known paths need no prefix joining.
     *
     * The well-known-loader probe and the boot-spec scan below are additive
     * and independent: a stick may carry a GRUB or systemd-boot BOOTAA64.EFI,
     * a /loader/entries directory, or both, and offering only one of them
     * because the other was missing would hide a bootable medium.
     */
    if (SfbFileExists (Root, SFB_BOOT_FILE_PATH)) {
      /*
       * \EFI\DESC names the loader when the medium bothers to; volumes
       * without one are numbered off in the order they were found, so every
       * row still has a label the user can tell apart.
       */
      Desc[0] = L'\0';
      SfbReadAnsiDescription (Root, SFB_DESC_FILE_PATH, Desc, SFB_DESC_CHARS);
      if (Desc[0] == L'\0') {
        UnicodeSPrint (Desc, sizeof (Desc), L"NONAME%u", NoName++);
      }

      Slot = &Menu->Entry[Menu->Count];
      Status = SfbMakeFileEntry (Volumes[Index], SFB_BOOT_FILE_PATH, Desc,
                                 Slot);
      if (!EFI_ERROR (Status)) {
        Duplicate = FALSE;
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
          DEBUG ((EFI_D_INFO, "SFB: discovered '%s' on volume %u\n",
                  Desc, (UINT32)Index));
          Menu->Count++;
        }
      }
    }

    /* Boot-spec entries are removable-media only: an internal ESP must not be
     * able to inject rows into the on-device boot root's menu. */
    if (SfbIsUsbVolume (Volumes[Index])) {
      SfbScanBlsEntries (Menu, Volumes[Index], Root);
    }

    Root->Close (Root);
  }

  FreePool (Volumes);
}

STATIC
VOID
SfbAppendConfigEntries (IN OUT SFB_MENU_STATE       *Menu,
                        IN CONST SFB_CONFIG         *Config,
                        IN EFI_HANDLE                Volume)
{
  EFI_FILE_PROTOCOL *Root = NULL;
  UINTN ConfigIndex;

  if (Config == NULL || Volume == NULL ||
      EFI_ERROR (SfbOpenVolumeRoot (Volume, &Root)) || Root == NULL) {
    return;
  }

  for (ConfigIndex = 0;
       ConfigIndex < Config->Count && Menu->Count < SFB_MAX_ENTRIES;
       ConfigIndex++) {
    CHAR16 RelPath[SFB_PATH_CHARS];
    CHAR16 Path[SFB_PATH_CHARS];
    CHAR16 Title[SFB_DESC_CHARS];
    SFB_BOOT_ENTRY *Slot;
    Path[0] = L'\0';

    SfbAsciiToUnicode (Config->Entry[ConfigIndex].Image, RelPath,
                       ARRAY_SIZE (RelPath));
    if (EFI_ERROR (SfbJoinRoot (SfbVolumeRootPrefix (Volume), RelPath, Path,
                                ARRAY_SIZE (Path))) ||
        !SfbFileExists (Root, Path)) {
      DEBUG ((EFI_D_INFO, "SFB: config entry '%a' image '%s' not present\n",
              Config->Entry[ConfigIndex].Id, Path));
      continue;
    }

    SfbAsciiToUnicode (Config->Entry[ConfigIndex].Title, Title,
                       ARRAY_SIZE (Title));
    Slot = &Menu->Entry[Menu->Count];
    if (EFI_ERROR (SfbMakeFileEntry (Volume, Path, Title, Slot))) {
      continue;
    }
    Slot->Mode = (SFB_BOOT_MODE)SfbConfigEntryMode (
                                  Config, &Config->Entry[ConfigIndex]);
    Slot->ModeFromConfig = TRUE;
    Slot->Role = Config->Entry[ConfigIndex].Role;
    /*
     * An `options` value rides in the same out-of-line payload table the
     * boot-spec rows use, rather than growing SFB_BOOT_ENTRY: the menu
     * embeds 32 of those by value. Kind stays SfbEntryEfiFile - this is a
     * plain application launch that happens to carry arguments, not a
     * boot-spec entry - so the launch path keys off BlsIndex alone.
     */
    if (Config->Entry[ConfigIndex].Options[0] != '\0') {
      SFB_BLS_ENTRY *Payload = &mSfbBlsPayload[Menu->Count];

      /* The payload was just zeroed and Options is NUL-terminated by the
       * parser, which refuses an over-long value rather than truncating, so
       * copying the whole fixed-size source is bounded and leaves the tail
       * of the wider destination zero. */
      ZeroMem (Payload, sizeof (*Payload));
      CopyMem (Payload->Cmdline, Config->Entry[ConfigIndex].Options,
               SFB_CONFIG_OPTIONS_CHARS);
      Slot->BlsIndex = (UINT8)Menu->Count;
    }
    if (Config->DefaultIndex == ConfigIndex) {
      Menu->DefaultIndex = Menu->Count;
    }
    Menu->Count++;

  }
  Root->Close (Root);
}

/* Case-insensitive ASCII suffix test, for the slot markers in config fields. */
STATIC
BOOLEAN
SfbAsciiEndsWith (IN CONST CHAR8 *Text, IN CONST CHAR8 *Suffix)
{
  UINTN  TextLength;
  UINTN  SuffixLength;
  UINTN  Index;

  if (Text == NULL || Suffix == NULL) {
    return FALSE;
  }
  TextLength = AsciiStrLen (Text);
  SuffixLength = AsciiStrLen (Suffix);
  if (SuffixLength == 0 || SuffixLength > TextLength) {
    return FALSE;
  }

  for (Index = 0; Index < SuffixLength; Index++) {
    CHAR8  Left = Text[TextLength - SuffixLength + Index];
    CHAR8  Right = Suffix[Index];

    if (Left >= 'A' && Left <= 'Z') {
      Left = (CHAR8)(Left + ('a' - 'A'));
    }
    if (Right >= 'A' && Right <= 'Z') {
      Right = (CHAR8)(Right + ('a' - 'A'));
    }
    if (Left != Right) {
      return FALSE;
    }
  }
  return TRUE;
}

/*
 * The slot a config entry claims, or SfbSlotUnknown when it claims none.
 *
 * Two things in an entry can name a slot, and the installers use both. The
 * inactive entry gets a slot-suffixed image (`boot_a.efi` / `boot_b.efi`),
 * which is the more specific statement and so wins. The active entry does not:
 * its image is always plain `boot.efi`, and the only thing that varies with the
 * slot is its id (`android-a` / `android-b`). Reading the image alone would
 * therefore call every active entry slot A and fire a mismatch on every device
 * running from slot B.
 *
 * An entry that names no slot at all - a hand-written config, a Linux entry -
 * answers Unknown and is never checked. Verification must have something
 * definite to disagree with before it withholds a boot.
 */
STATIC
SFB_SLOT
SfbConfigEntrySlot (IN CONST CHAR8 *Id, IN CONST CHAR8 *Image)
{
  if (SfbAsciiEndsWith (Image, "_a.efi")) {
    return SfbSlotA;
  }
  if (SfbAsciiEndsWith (Image, "_b.efi")) {
    return SfbSlotB;
  }
  if (SfbAsciiEndsWith (Id, "-a") || SfbAsciiEndsWith (Id, "_a")) {
    return SfbSlotA;
  }
  if (SfbAsciiEndsWith (Id, "-b") || SfbAsciiEndsWith (Id, "_b")) {
    return SfbSlotB;
  }
  return SfbSlotUnknown;
}

/*
 * Check the config's `active` label against the slot the GPT marks active.
 *
 * Roles are presentation-only and are written by whoever authored the config.
 * When an OTA flips the active slot and the device-side watcher has not run
 * yet - because Android has not booted - the config still labels the old slot
 * active and `default` still points at it. Nothing else in this design can
 * notice, and the BDS would unattended-launch an entry the config no longer
 * describes correctly, on a device one bad boot from EDL.
 *
 * Verification only: no entry is ever selected by slot and no slot is ever
 * written. The config author still decides what boots.
 */
STATIC
VOID
SfbCheckConfigSlots (IN OUT SFB_MENU_STATE *Menu, IN CONST SFB_CONFIG *Config)
{
  SFB_SLOT  Active;
  UINTN     Index;

  Active = SfbActiveSlot ();
  if (Active == SfbSlotUnknown) {
    return;
  }

  for (Index = 0; Index < Config->Count; Index++) {
    SFB_SLOT  Claimed;

    if (Config->Entry[Index].Role != SfbConfigRoleActive) {
      continue;
    }
    Claimed = SfbConfigEntrySlot (Config->Entry[Index].Id,
                                  Config->Entry[Index].Image);
    if (Claimed != SfbSlotUnknown && Claimed != Active) {
      DEBUG ((EFI_D_WARN,
              "SFB: MARK slot-stale entry='%a' image='%a'\n",
              Config->Entry[Index].Id, Config->Entry[Index].Image));
      Menu->SlotMismatch = TRUE;
    }
  }
}

STATIC
VOID
SfbResolveDefault (IN OUT SFB_MENU_STATE *Menu,
                   IN CONST SFB_CONFIG   *Config)
{
  UINTN Index;

  Menu->DefaultFromConfig = FALSE;

  if (Config != NULL && Config->Valid &&
      Config->DefaultIndex != SFB_CONFIG_NO_DEFAULT &&
      Config->DefaultIndex < Config->Count &&
      Menu->DefaultIndex != SFB_NO_INDEX &&
      Menu->DefaultIndex < Menu->Count) {
    /*
     * A stale `active` label must not boot unattended: the entry it points at
     * is the one the flipped slot invalidated. The row stays highlighted so it
     * is still one keypress away, but the user has to look first. A default
     * carrying any other role is untouched - only `active` makes a claim about
     * the slot.
     */
    Menu->DefaultFromConfig =
      (BOOLEAN)!(Menu->SlotMismatch &&
                 Menu->Entry[Menu->DefaultIndex].Role == SfbConfigRoleActive);
    return;
  }

  Menu->DefaultIndex = SFB_NO_INDEX;
  for (Index = 0; Index < Menu->Count; Index++) {
    /* Removable media is never the unattended starting point: its device path
     * does not survive a reboot, so a USB row either fails to resolve or
     * resolves onto whatever disk happens to be plugged in next. */
    if (Menu->Entry[Index].Kind == SfbEntryEfiFile &&
        !Menu->Entry[Index].IsUsb) {
      Menu->DefaultIndex = Index;
      break;
    }
  }
}

VOID
SfbBuildMenu (OUT SFB_MENU_STATE *Menu, IN SFB_BOOT_MODE Mode)
{
  EFI_STATUS Status;
  EFI_HANDLE ConfigVolume = NULL;
  SFB_CONFIG Config;
  /* Fastboot, Selector, Tools, Mass Storage, Recovery, Power Off, Restart:
   * the rows appended after truncation, whose space the discovered entries
   * must not eat. */
  UINTN MandatoryRows = 7;
  UINTN ReservedRows;
  UINTN Unconfigured;
  UINTN Index;

  ZeroMem (Menu, sizeof (*Menu));
  Menu->Mode = Mode;
  Menu->DefaultIndex = SFB_NO_INDEX;
  Menu->TimeoutSeconds = SFB_CONFIG_DEFAULT_TIMEOUT;
  Menu->LockPolicy = SfbConfigLockAsNeeded;

  SfbBootMark (L"menu:begin");
  /* The mode row is a session-only override, never a persisted setting. */
  SfbAppendBuiltIn (Menu, SfbEntryMode, L"Session boot mode");

  SfbBootMark (L"menu:config");

  Status = SfbLoadBootConfig (&Config, &ConfigVolume);
  if (!EFI_ERROR (Status)) {
    Menu->ConfigValid = TRUE;
    Menu->ConfigGeneration = Config.Generation;
    Menu->TimeoutSeconds = Config.TimeoutSeconds;
    Menu->LockPolicy = Config.LockPolicy;
    Menu->RejectedLines = Config.RejectedLines;
    SfbAppendConfigEntries (Menu, &Config, ConfigVolume);
    SfbCheckConfigSlots (Menu, &Config);
  }

  /*
   * Every row appended from here on is one no config describes: the known-name
   * probe that stands in for an absent canoe.cfg, and the removable discovery
   * that runs beside a valid one. Both launch under the session mode.
   */
  Unconfigured = Menu->Count;

  if (!Menu->ConfigValid) {
    SfbBootMark (L"menu:bootroot");
    SfbAppendBootRootEntries (Menu);
  }

  /*
   * Discovery comes after the configured rows on purpose. It keeps every config
   * row's index - and with it Menu->DefaultIndex, set by SfbAppendConfigEntries
   * - the same no matter what is plugged in, and it makes the truncation below
   * shed discovered rows rather than configured ones when the budget is hit.
   */
  SfbBootMark (L"menu:removable");
  SfbScanRemovableVolumes (Menu);
  SfbBootMark (L"menu:rows");

  for (Index = Unconfigured; Index < Menu->Count; Index++) {
    Menu->Entry[Index].Mode = Mode;
    Menu->Entry[Index].ModeFromConfig = FALSE;
  }

  ReservedRows = MandatoryRows +
                 ((Menu->ConfigValid && Menu->RejectedLines != 0) ? 1 : 0) +
                 (Menu->SlotMismatch ? 1 : 0);
  while (Menu->Count > SFB_MAX_ENTRIES - ReservedRows) {
    Menu->Count--;
    SfbFreeEntry (&Menu->Entry[Menu->Count]);
  }

  if (Menu->ConfigValid && Menu->RejectedLines != 0) {
    CHAR16 Rejected[SFB_DESC_CHARS];

    UnicodeSPrint (Rejected, sizeof (Rejected),
                   L"Config rejected lines: %u",
                   (UINT32)Menu->RejectedLines);
    SfbAppendBuiltIn (Menu, SfbEntryBack, Rejected);
  }

  if (Menu->SlotMismatch) {
    SfbAppendBuiltIn (Menu, SfbEntryBack, L"Config slot role is stale");
  }

  SfbAppendBuiltIn (Menu, SfbEntryFastboot, L"Enter Fastboot");
  SfbAppendBuiltIn (Menu, SfbEntrySelector, L"Enter EFI Program Selector");
  SfbAppendBuiltIn (Menu, SfbEntryTools, L"EFI Tools");
  SfbAppendBuiltIn (Menu, SfbEntryMassStorage, L"USB Mass Storage");
  SfbAppendBuiltIn (Menu, SfbEntryRecovery, L"Reboot to Recovery");
  SfbAppendBuiltIn (Menu, SfbEntryPowerOff, L"Power Off");
  SfbAppendBuiltIn (Menu, SfbEntryRestart, L"Restart");

  SfbResolveDefault (Menu, &Config);
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
  /* A rebuilt menu must not be able to resurrect a payload belonging to a row
   * that is no longer there; the index is only a byte and BlsIndex slots are
   * reused across rebuilds. */
  ZeroMem (mSfbBlsPayload, sizeof (mSfbBlsPayload));
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
  /*
   * Managed launches are a property of the on-device boot root, never of the
   * media. Without the IsUsb test a stick with \boot.efi at its root would be
   * launched with the full policy projection and the efisp hide armed, purely
   * because its filename matched.
   */
  return (BOOLEAN)(!Entry->IsUsb && SfbIsManagedAblPath (Entry->Path));
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

/*
 * Launch a Boot Loader Specification row.
 *
 * Never managed: the row came off removable media, so no policy wrapper is
 * installed and the ABL sidecars have nothing to say about it. What it does
 * carry is the three things a kernel needs and an ordinary EFI application
 * does not get from a device path: a command line, an initrd and a DTB.
 *
 * Either publication failing aborts the launch with that status and no side
 * effects. A half-published initrd is worse than no boot: the kernel would
 * come up with a configuration nobody asked for.
 */
STATIC
EFI_STATUS
SfbLaunchBlsEntry (IN CONST SFB_BOOT_ENTRY *Entry,
                   IN SFB_BOOT_MODE        EffectiveMode)
{
  EFI_STATUS           Status;
  CONST SFB_BLS_ENTRY  *Payload = SfbBlsPayload (Entry->BlsIndex);
  CHAR16               Cmdline[SFB_BLS_CMDLINE_CHARS];
  CHAR16               Path[SFB_PATH_CHARS];
  CONST CHAR16         *Options = NULL;
  BOOLEAN              DtbUp = FALSE;
  BOOLEAN              InitrdUp = FALSE;

  if (Payload == NULL || Payload->Kind == SfbBlsKindNone) {
    return EFI_INVALID_PARAMETER;
  }

  if (Payload->Cmdline[0] != '\0') {
    SfbAsciiToUnicode (Payload->Cmdline, Cmdline, ARRAY_SIZE (Cmdline));
    Options = Cmdline;
  }

  /* An `efi` row is a plain application launch with arguments; only a `linux`
   * row has a stub on the other end that reads these two. */
  if (Entry->Kind == SfbEntryBlsLinux) {
    if (Payload->Dtb[0] != '\0') {
      SfbAsciiToUnicode (Payload->Dtb, Path, ARRAY_SIZE (Path));
      Status = SfbDtbInstall (Entry->Volume, Path);
      if (EFI_ERROR (Status)) {
        return Status;
      }
      DtbUp = TRUE;
    }
    if (Payload->Initrd[0] != '\0') {
      SfbAsciiToUnicode (Payload->Initrd, Path, ARRAY_SIZE (Path));
      Status = SfbInitrdInstall (Entry->Volume, Path);
      if (EFI_ERROR (Status)) {
        if (DtbUp) {
          SfbDtbUninstall ();
        }
        return Status;
      }
      InitrdUp = TRUE;
    }
  }

  SfbBypassSecurity ();
  Status = SfbLaunchImage (Entry->DevicePath, FALSE, EffectiveMode, NULL,
                           NULL, Options);

  /* Return path only: a kernel that booted never comes back here, and a
   * kernel that returned must not leave a stale configuration table or a
   * stray LoadFile2 handle for the next launch. */
  if (InitrdUp) {
    SfbInitrdUninstall ();
  }
  if (DtbUp) {
    SfbDtbUninstall ();
  }
  return Status;
}

EFI_STATUS
SfbLaunchEntry (IN CONST SFB_BOOT_ENTRY *Entry,
                IN BOOLEAN              ClearScreen,
                IN SFB_BOOT_MODE        SessionMode)
{
  EFI_STATUS Status;
  BOOLEAN Managed;
  SFB_BOOT_MODE EffectiveMode;
  SFB_MODE2_PROFILE Profile;
  SFB_TZ_MAP TzMap;
  CONST SFB_TZ_MAP *TzMapPtr = NULL;
  /* Returned children and failed launches must never leave a policy wrapper
   * active while DRIVER.LIST images are loaded. */
  SfbDisarmManagedAblHooks ();

  if (Entry == NULL || Entry->DevicePath == NULL ||
      (Entry->Kind != SfbEntryEfiFile &&
       Entry->Kind != SfbEntryBlsLinux &&
       Entry->Kind != SfbEntryBlsEfi)) {
    return EFI_INVALID_PARAMETER;
  }
  EffectiveMode = Entry->ModeFromConfig ? Entry->Mode : SessionMode;
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

  if (Managed && EffectiveMode == SfbBootModeKmProfile) {
    EFI_STATUS ProfileStatus;

    Status = SfbResolveManagedAblMode (Entry, EffectiveMode, &EffectiveMode,
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
          "effective-mode=%u\n",
          (UINT32)Managed, (UINT32)SessionMode, (UINT32)EffectiveMode));

  SfbPreloadDrivers (Entry->Volume, Entry->Path);

  if (Entry->Kind == SfbEntryBlsLinux || Entry->Kind == SfbEntryBlsEfi) {
    Status = SfbLaunchBlsEntry (Entry, EffectiveMode);
  } else {
    /*
     * A plain application launch, with arguments when the config row asked
     * for them. This is what makes a loader row work: the image is
     * \tools\FdLoader.efi or \tools\AbootLoader.efi and the payload it
     * should boot is named in `options`. Without this the loaders receive
     * an empty command line and can only print their usage.
     */
    CONST SFB_BLS_ENTRY  *Payload = (Entry->BlsIndex != SFB_NO_BLS)
                                      ? SfbBlsPayload (Entry->BlsIndex)
                                      : NULL;
    CHAR16               Options[SFB_BLS_CMDLINE_CHARS];
    CONST CHAR16         *OptionsPtr = NULL;

    if (Payload != NULL && Payload->Cmdline[0] != '\0') {
      SfbAsciiToUnicode (Payload->Cmdline, Options, ARRAY_SIZE (Options));
      OptionsPtr = Options;
    }

    SfbBypassSecurity ();
    Status = SfbLaunchImage (
               Entry->DevicePath,
               Managed,
               EffectiveMode,
               EffectiveMode == SfbBootModeKmProfile ? &Profile : NULL,
               TzMapPtr,
               OptionsPtr);
  }
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
  HasDefault = (BOOLEAN)(Menu.DefaultFromConfig &&
                         Menu.DefaultIndex != SFB_NO_INDEX);
  if (HasDefault) {
    DEBUG ((EFI_D_INFO, "SFB: launching default entry '%s'\n",
            Menu.Entry[Menu.DefaultIndex].Desc));
    SfbSetLaunchLockPolicy (Menu.ConfigValid ? Menu.LockPolicy
                                             : SfbConfigLockAsNeeded);
    SfbLaunchEntry (&Menu.Entry[Menu.DefaultIndex], FALSE, Mode);
  }
  SfbFreeMenu (&Menu);
  return HasDefault;
}