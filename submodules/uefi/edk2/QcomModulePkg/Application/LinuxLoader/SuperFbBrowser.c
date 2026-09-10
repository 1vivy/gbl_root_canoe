/*
 * File browser for the super-fastboot boot menu: the managed FAT boot root
 * and removable boot volumes.
 *
 * Pick a volume, walk directories with the volume keys, and launch an EFI
 * application for this boot only.
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbMenu.h"
#include "SuperFbContainer.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Guid/FileInfo.h>

/* Keeps the translation unit legal when the feature is compiled out. */
CONST CHAR8 *gSfbBrowserModuleTag = "SuperFbBrowser";

/* SFB_NAME_CHARS and SFB_DIR_ENTRY moved to SuperFbMenu.h when the boot-spec
 * scan in SuperFbEntries.c became a second reader of directories. */

/* ---- path helpers ------------------------------------------------------- */

/* Shared with SuperFbEntries.c via SuperFbMenu.h: both join paths and must
 * agree on whether a separator is already present. */
BOOLEAN
SfbIsRootPath (IN CONST CHAR16 *Path)
{
  return (BOOLEAN)(Path[0] == L'\\' && Path[1] == L'\0');
}

STATIC
EFI_STATUS
SfbJoinPath (IN OUT CHAR16    *Path,
             IN UINTN          PathChars,
             IN CONST CHAR16  *Name)
{
  RETURN_STATUS  Status;
  UINTN          PathLength;
  UINTN          NameLength;

  if (Path == NULL || Name == NULL || PathChars == 0) {
    return EFI_INVALID_PARAMETER;
  }

  PathLength = StrLen (Path);
  NameLength = StrLen (Name);
  if (PathLength >= PathChars) {
    return EFI_BUFFER_TOO_SMALL;
  }

  if (SfbIsRootPath (Path)) {
    if (NameLength >= PathChars - PathLength) {
      return EFI_BUFFER_TOO_SMALL;
    }
  } else if (PathLength + 1 >= PathChars ||
             NameLength >= PathChars - PathLength - 1) {
    return EFI_BUFFER_TOO_SMALL;
  }

  if (!SfbIsRootPath (Path)) {
    Status = StrCatS (Path, PathChars, L"\\");
    if (RETURN_ERROR (Status)) {
      return (EFI_STATUS)Status;
    }
  }
  Status = StrCatS (Path, PathChars, Name);
  return RETURN_ERROR (Status) ? (EFI_STATUS)Status : EFI_SUCCESS;
}

STATIC
VOID
SfbParentPath (IN OUT CHAR16 *Path)
{
  UINTN  Index;

  if (SfbIsRootPath (Path)) {
    return;
  }

  for (Index = StrLen (Path); Index > 0; Index--) {
    if (Path[Index - 1] == L'\\') {
      break;
    }
  }

  /* Index now sits just past the separator that starts the last component. */
  if (Index <= 1) {
    Path[0] = L'\\';
    Path[1] = L'\0';
  } else {
    Path[Index - 1] = L'\0';
  }
}

STATIC
BOOLEAN
SfbIsEfiFile (IN CONST CHAR16 *Name)
{
  UINTN         Length = StrLen (Name);
  CONST CHAR16  *Ext;

  if (Length < 5) {
    return FALSE;
  }

  Ext = Name + Length - 4;

  return (BOOLEAN)(Ext[0] == L'.' &&
                   (Ext[1] == L'e' || Ext[1] == L'E') &&
                   (Ext[2] == L'f' || Ext[2] == L'F') &&
                   (Ext[3] == L'i' || Ext[3] == L'I'));
}

/* ---- directory listing -------------------------------------------------- */

/* Parent row first, then directories, then files, each alphabetically. */
STATIC
INTN
SfbCompareDirEntries (IN CONST SFB_DIR_ENTRY *A, IN CONST SFB_DIR_ENTRY *B)
{
  UINTN  RankA = A->IsParent ? 0 : (A->IsDir ? 1 : 2);
  UINTN  RankB = B->IsParent ? 0 : (B->IsDir ? 1 : 2);

  if (RankA != RankB) {
    return (RankA < RankB) ? -1 : 1;
  }

  return StrCmp (A->Name, B->Name);
}

STATIC
BOOLEAN
SfbCopyDirectoryName (OUT CHAR16       *Destination,
                      IN CONST CHAR16  *Source)
{
  if (Destination == NULL || Source == NULL ||
      StrLen (Source) >= SFB_NAME_CHARS) {
    return FALSE;
  }
  return (BOOLEAN)!RETURN_ERROR (StrCpyS (Destination, SFB_NAME_CHARS, Source));
}

STATIC
VOID
SfbSortDirEntries (IN OUT SFB_DIR_ENTRY *List, IN UINTN Count)
{
  UINTN          Index;
  UINTN          Probe;

  SFB_DIR_ENTRY  Pending;

  for (Index = 1; Index < Count; Index++) {
    CopyMem (&Pending, &List[Index], sizeof (Pending));

    for (Probe = Index;
         Probe > 0 && SfbCompareDirEntries (&List[Probe - 1], &Pending) > 0;
         Probe--) {
      CopyMem (&List[Probe], &List[Probe - 1], sizeof (Pending));
    }

    CopyMem (&List[Probe], &Pending, sizeof (Pending));
  }
}

/*
 * Fill List with the contents of Dir, preceded by a synthetic ".." row.
 * Truncated is set when the directory holds more than SFB_MAX_DIR_ENTRIES
 * items, so the caller can say so rather than silently hiding them.
 */
EFI_STATUS
SfbReadDirectory (IN EFI_FILE_PROTOCOL  *Dir,
                  OUT SFB_DIR_ENTRY     *List,
                  IN UINTN              Max,
                  OUT UINTN             *Count,
                  OUT BOOLEAN           *Truncated)
{
  EFI_STATUS     Status;
  EFI_FILE_INFO  *Info;
  UINTN          InfoSize;
  UINTN          BufferSize;

  *Count = 0;
  *Truncated = FALSE;

  if (Max == 0) {
    return EFI_INVALID_PARAMETER;
  }

  /* The ".." row always exists: at the root it backs out to the volume list. */
  ZeroMem (&List[0], sizeof (List[0]));
  StrCpyS (List[0].Name, SFB_NAME_CHARS, L"..");
  List[0].IsDir = TRUE;
  List[0].IsParent = TRUE;
  *Count = 1;

  InfoSize = sizeof (EFI_FILE_INFO) + SFB_NAME_CHARS * sizeof (CHAR16);
  Info = AllocateZeroPool (InfoSize);
  if (Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = Dir->SetPosition (Dir, 0);
  if (EFI_ERROR (Status)) {
    FreePool (Info);
    return Status;
  }

  while (TRUE) {
    BufferSize = InfoSize;
    Status = Dir->Read (Dir, &BufferSize, Info);

    if (Status == EFI_BUFFER_TOO_SMALL) {
      /* A name longer than we budgeted for; grow once and retry this entry. */
      EFI_FILE_INFO  *Bigger = AllocateZeroPool (BufferSize);

      if (Bigger == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        break;
      }
      FreePool (Info);
      Info = Bigger;
      InfoSize = BufferSize;
      continue;
    }

    if (EFI_ERROR (Status)) {
      break;
    }

    /* A zero-length read marks the end of the directory. */
    if (BufferSize == 0) {
      Status = EFI_SUCCESS;
      break;
    }

    /* We supply our own parent row and have no use for ".". */
    if (StrCmp (Info->FileName, L".") == 0 ||
        StrCmp (Info->FileName, L"..") == 0) {
      continue;
    }

    if (StrLen (Info->FileName) >= SFB_NAME_CHARS) {
      DEBUG ((EFI_D_WARN,
              "SFB: directory entry name too long; skipped\n"));
      continue;
    }

    if (*Count >= Max) {
      *Truncated = TRUE;
      Status = EFI_SUCCESS;
      break;
    }

    ZeroMem (&List[*Count], sizeof (List[0]));
    if (!SfbCopyDirectoryName (List[*Count].Name, Info->FileName)) {
      continue;
    }
    List[*Count].IsDir =
      (BOOLEAN)((Info->Attribute & EFI_FILE_DIRECTORY) != 0);
    (*Count)++;
  }

  FreePool (Info);

  SfbSortDirEntries (List, *Count);

  return Status;
}

STATIC
EFI_STATUS
SfbOpenDirectory (IN EFI_HANDLE          Volume,
                  IN CONST CHAR16        *Path,
                  OUT EFI_FILE_PROTOCOL  **Root,
                  OUT EFI_FILE_PROTOCOL  **Dir)
{
  EFI_STATUS  Status;

  *Root = NULL;
  *Dir = NULL;

  Status = SfbOpenVolumeRoot (Volume, Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (SfbIsRootPath (Path)) {
    *Dir = *Root;
    return EFI_SUCCESS;
  }

  Status = (*Root)->Open (*Root, Dir, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    (*Root)->Close (*Root);
    *Root = NULL;
    *Dir = NULL;
  }

  return Status;
}

/* ---- action menu for a chosen EFI application --------------------------- */

typedef struct {
  EFI_HANDLE       Volume;
  CONST CHAR16    *FullPath;
} SFB_DRIVER_MENU_CONTEXT;

STATIC
SFB_MENU_ACTION
SfbHandleDriverMenuRow (IN VOID *Context,
                        IN UINTN Row,
                        IN SFB_KEY Key)
{
  SFB_DRIVER_MENU_CONTEXT *State = (SFB_DRIVER_MENU_CONTEXT *)Context;
  EFI_STATUS               Status;

  (VOID)Key;
  if (Row != 0) {
    return SfbMenuActionExit;
  }
  /* Load: start the driver, then connect controllers so it binds. */
  Status = SfbLoadDriver (State->Volume, State->FullPath);
  if (!EFI_ERROR (Status)) {
    SfbConnectAll ();
  }
  SfbReportStatus (EFI_ERROR (Status) ? L"Driver load failed"
                                      : L"Driver loaded", Status);
  return SfbMenuActionContinue;
}

/*
 * A UEFI driver is loaded, not booted: it installs protocols and returns rather
 * than taking over the machine. Offer just that. Never unwinds to the boot menu
 * (a driver is not a boot entry), so this always returns FALSE.
 */
STATIC
BOOLEAN
SfbDriverActionMenu (IN EFI_HANDLE   Volume,
                     IN CONST CHAR16 *FullPath)
{
  STATIC SFB_MENU_ROW Rows[] = {
    { L"Load", L" " },
    { L"Back", L" " }
  };
  SFB_DRIVER_MENU_CONTEXT Context;
  SFB_MENU_TEMPLATE       Template;

  ZeroMem (&Context, sizeof (Context));
  ZeroMem (&Template, sizeof (Template));
  Context.Volume = Volume;
  Context.FullPath = FullPath;
  Template.Title = L"EFI Driver";
  Template.Subtitle = FullPath;
  Template.Footer = L"Vol Up/Down: move   Power: select";
  Template.Rows = Rows;
  Template.RowCount = ARRAY_SIZE (Rows);
  Template.Navigate = TRUE;
  Template.Context = &Context;
  Template.Handler = SfbHandleDriverMenuRow;
  (VOID)SfbRunMenu (&Template);
  return FALSE;
}

typedef struct {
  SFB_BOOT_ENTRY Entry;
  SFB_BOOT_MODE  Mode;
} SFB_EFI_MENU_CONTEXT;

STATIC
VOID
SfbExitEfiMenu (IN VOID *Context)
{
  SFB_EFI_MENU_CONTEXT *State = (SFB_EFI_MENU_CONTEXT *)Context;

  SfbFreeEntry (&State->Entry);
}

STATIC
SFB_MENU_ACTION
SfbHandleEfiMenuRow (IN VOID *Context,
                     IN UINTN Row,
                     IN SFB_KEY Key)
{
  SFB_EFI_MENU_CONTEXT *State = (SFB_EFI_MENU_CONTEXT *)Context;
  EFI_STATUS             Status;

  (VOID)Key;
  if (Row != 0) {
    return SfbMenuActionExit;
  }
  /* Browsed images are explicitly temporary and never become a menu row. */
  Status = SfbLaunchEntry (&State->Entry, TRUE, State->Mode);
  if (EFI_ERROR (Status)) {
    SfbReportStatus (L"Boot failed", Status);
  }
  return SfbMenuActionContinue;
}

/*
 * Offer what can be done with one .efi. Returns TRUE when the browser should
 * unwind all the way back to the boot menu after a launch.
 */
STATIC
BOOLEAN
SfbEfiActionMenu (IN EFI_HANDLE    Volume,
                  IN CONST CHAR16  *FullPath,
                  IN CONST CHAR16  *Name,
                  IN SFB_BOOT_MODE  Mode)
{
  STATIC SFB_MENU_ROW Rows[] = {
    { L"Boot (this boot)", L" " },
    { L"Back", L" " }
  };
  SFB_EFI_MENU_CONTEXT Context;
  SFB_MENU_TEMPLATE     Template;
  EFI_FILE_PROTOCOL    *Root = NULL;
  EFI_STATUS            Status;
  BOOLEAN               IsDriver = FALSE;

  /* A driver image gets its own Load/Back menu rather than the boot actions. */
  if (!EFI_ERROR (SfbOpenVolumeRoot (Volume, &Root)) && Root != NULL) {
    IsDriver = SfbIsEfiDriverFile (Root, FullPath);
    Root->Close (Root);
  }
  if (IsDriver) {
    return SfbDriverActionMenu (Volume, FullPath);
  }

  ZeroMem (&Context, sizeof (Context));
  Status = SfbMakeFileEntry (Volume, FullPath, Name, &Context.Entry);
  if (EFI_ERROR (Status)) {
    SfbReportStatus (L"Cannot address that file", Status);
    return FALSE;
  }
  Context.Mode = Mode;

  ZeroMem (&Template, sizeof (Template));
  Template.Title = L"EFI Application";
  Template.Subtitle = FullPath;
  Template.Footer = L"Vol Up/Down: move   Power: select";
  Template.Rows = Rows;
  Template.RowCount = ARRAY_SIZE (Rows);
  Template.Navigate = TRUE;
  Template.Context = &Context;
  Template.Exit = SfbExitEfiMenu;
  Template.Handler = SfbHandleEfiMenuRow;
  (VOID)SfbRunMenu (&Template);
  return FALSE;
}

/* ---- directory navigation ----------------------------------------------- */

typedef struct {
  EFI_HANDLE          Volume;
  CONST CHAR16       *BrowseRoot;
  CHAR16              Path[SFB_PATH_CHARS];
  SFB_DIR_ENTRY      *List;
  UINTN               Count;
  SFB_BOOT_MODE       Mode;
  BOOLEAN             Truncated;
  BOOLEAN             Unwind;
  SFB_MENU_TEMPLATE  *Template;
} SFB_DIRECTORY_CONTEXT;

STATIC EFI_STATUS
SfbRefreshDirectory (IN VOID *Context)
{
  SFB_DIRECTORY_CONTEXT *State = Context;
  EFI_STATUS Status;

  while (TRUE) {
    EFI_FILE_PROTOCOL *Root = NULL;
    EFI_FILE_PROTOCOL *Dir = NULL;
    Status = SfbOpenDirectory (State->Volume, State->Path, &Root, &Dir);
    if (!EFI_ERROR (Status)) {
      Status = SfbReadDirectory (Dir, State->List, SFB_MAX_DIR_ENTRIES,
                                 &State->Count, &State->Truncated);
      if (Dir != Root) { Dir->Close (Dir); }
      Root->Close (Root);
    }
    if (!EFI_ERROR (Status)) { break; }
    SfbReportStatus (L"Cannot read directory", Status);
    if (StrCmp (State->Path, State->BrowseRoot) == 0) { return Status; }
    SfbParentPath (State->Path);
    State->Template->Cursor = 0;
  }
  State->Template->RowCount = State->Count;
  return EFI_SUCCESS;
}

STATIC VOID
SfbDrawDirectoryHeader (IN VOID *Context)
{
  SFB_DIRECTORY_CONTEXT *State = Context;
  SfbDrawWrappedInfo (State->Path);
  if (State->Truncated) { SfbDrawWrappedInfo (L"Additional directory entries are not shown."); }
}

STATIC VOID
SfbDrawDirectoryRow (IN VOID *Context, IN UINTN Row, IN BOOLEAN Selected)
{
  SFB_DIRECTORY_CONTEXT *State = Context;
  CONST SFB_DIR_ENTRY *Entry = &State->List[Row];
  CONST CHAR16 *Marker = Entry->IsDir ? L"D" : SfbIsEfiFile (Entry->Name) ? L"E" : L" ";
  SfbDrawRow (Selected, Marker, Entry->Name);
}

STATIC SFB_MENU_ACTION
SfbHandleDirectoryRow (IN VOID *Context, IN UINTN Row, IN SFB_KEY Key)
{
  SFB_DIRECTORY_CONTEXT *State = Context;
  CONST SFB_DIR_ENTRY *Selected;
  CHAR16 FullPath[SFB_PATH_CHARS];
  EFI_STATUS Status;
  if (Key != SfbKeySelect || Row >= State->Count) { return SfbMenuActionContinue; }
  Selected = &State->List[Row];
  if (Selected->IsParent) {
    if (StrCmp (State->Path, State->BrowseRoot) == 0) { return SfbMenuActionExit; }
    SfbParentPath (State->Path);
    State->Template->Cursor = 0;
    return SfbMenuActionRebuild;
  }
  if (Selected->IsDir) {
    Status = SfbJoinPath (State->Path, SFB_PATH_CHARS, Selected->Name);
    if (EFI_ERROR (Status)) {
      SfbReportStatus (L"Path too long", Status);
      return SfbMenuActionContinue;
    }
    State->Template->Cursor = 0;
    return SfbMenuActionRebuild;
  }
  if (!SfbIsEfiFile (Selected->Name)) {
    SfbReportStatus (L"Not an EFI application", EFI_UNSUPPORTED);
    return SfbMenuActionContinue;
  }
  StrCpyS (FullPath, SFB_PATH_CHARS, State->Path);
  Status = SfbJoinPath (FullPath, SFB_PATH_CHARS, Selected->Name);
  if (EFI_ERROR (Status)) {
    SfbReportStatus (L"Path too long", Status);
    return SfbMenuActionContinue;
  }
  if (SfbEfiActionMenu (State->Volume, FullPath, Selected->Name, State->Mode)) {
    State->Unwind = TRUE;
    return SfbMenuActionExit;
  }
  /* A temporary boot may have changed the volume underneath us. */
  return SfbMenuActionRebuild;
}

STATIC VOID
SfbExitDirectory (IN VOID *Context)
{
  SFB_DIRECTORY_CONTEXT *State = Context;
  FreePool (State->List);
  State->List = NULL;
}

/* Directory contents use the same drawing, scrolling, key dispatch and cleanup
 * as every other menu. Only opening paths and interpreting files belong here. */
STATIC BOOLEAN
SfbBrowseVolume (IN EFI_HANDLE Volume, IN CONST CHAR16 *VolumeLabel,
                 IN CONST CHAR16 *BrowseRoot, IN SFB_BOOT_MODE Mode)
{
  SFB_DIRECTORY_CONTEXT State;
  SFB_MENU_TEMPLATE Template;
  ZeroMem (&State, sizeof (State));
  ZeroMem (&Template, sizeof (Template));
  State.List = AllocateZeroPool (SFB_MAX_DIR_ENTRIES * sizeof (*State.List));
  if (State.List == NULL) {
    SfbReportStatus (L"Out of memory", EFI_OUT_OF_RESOURCES);
    return FALSE;
  }
  State.Volume = Volume;
  State.BrowseRoot = BrowseRoot;
  State.Mode = Mode;
  State.Template = &Template;
  StrCpyS (State.Path, SFB_PATH_CHARS, BrowseRoot);
  Template.Title = VolumeLabel;
  Template.Footer = L"Vol Up/Down: move   Power: open";
  Template.Context = &State;
  Template.Navigate = TRUE;
  Template.Refresh = SfbRefreshDirectory;
  Template.Exit = SfbExitDirectory;
  Template.Handler = SfbHandleDirectoryRow;
  Template.DrawHeader = SfbDrawDirectoryHeader;
  Template.DrawRow = SfbDrawDirectoryRow;
  (VOID)SfbRunMenu (&Template);
  return State.Unwind;
}

/* ---- volume selection --------------------------------------------------- */
typedef struct {
  CHAR16  Label[SFB_DESC_CHARS];
} SFB_VOLUME_ROW;


typedef struct {
  EFI_HANDLE      *Volumes;
  UINTN            VolumeCount;
  SFB_VOLUME_ROW  *Rows;
  SFB_BOOT_MODE    Mode;
} SFB_PROGRAM_MENU_CONTEXT;

STATIC
VOID
SfbDrawProgramMenuRow (IN VOID    *Context,
                       IN UINTN    Row,
                       IN BOOLEAN  Selected)
{
  SFB_PROGRAM_MENU_CONTEXT *State = (SFB_PROGRAM_MENU_CONTEXT *)Context;

  if (Row == State->VolumeCount) {
    SfbDrawRow (Selected, L" ", L"Back");
  } else {
    SfbDrawRow (Selected, L"V", State->Rows[Row].Label);
  }
}

STATIC
SFB_MENU_ACTION
SfbHandleProgramMenuRow (IN VOID *Context,
                         IN UINTN Row,
                         IN SFB_KEY Key)
{
  SFB_PROGRAM_MENU_CONTEXT *State = (SFB_PROGRAM_MENU_CONTEXT *)Context;
  CONST CHAR16             *Prefix;
  CONST CHAR16             *BrowseRoot;

  (VOID)Key;
  if (Row >= State->VolumeCount) {
    return SfbMenuActionExit;
  }

  Prefix = SfbVolumeRootPrefix (State->Volumes[Row]);
  BrowseRoot = (Prefix[0] == L'\0') ? L"\\" : Prefix;
  if (SfbBrowseVolume (State->Volumes[Row], State->Rows[Row].Label,
                       BrowseRoot, State->Mode)) {
    return SfbMenuActionExit;
  }
  return SfbMenuActionContinue;
}

STATIC
VOID
SfbExitProgramMenu (IN VOID *Context)
{
  SFB_PROGRAM_MENU_CONTEXT *State = (SFB_PROGRAM_MENU_CONTEXT *)Context;

  if (State->Rows != NULL) {
    FreePool (State->Rows);
    State->Rows = NULL;
  }
  if (State->Volumes != NULL) {
    FreePool (State->Volumes);
    State->Volumes = NULL;
  }
}

VOID
SfbRunFileBrowser (IN SFB_BOOT_MODE Mode)
{
  EFI_STATUS                Status;
  SFB_PROGRAM_MENU_CONTEXT  Context;
  SFB_MENU_TEMPLATE         Template;
  UINTN                     Index;

  ZeroMem (&Context, sizeof (Context));
  ZeroMem (&Template, sizeof (Template));

  /* Media may have been inserted since the loader started. */
  SfbStartFatStack ();
  Status = SfbLocateVolumes (&Context.Volumes, &Context.VolumeCount);
  if (EFI_ERROR (Status) || Context.Volumes == NULL ||
      Context.VolumeCount == 0) {
    SfbReportStatus (L"No boot volumes found",
                     EFI_ERROR (Status) ? Status : EFI_NOT_FOUND);
    if (Context.Volumes != NULL) {
      FreePool (Context.Volumes);
    }
    return;
  }

  Context.Rows = AllocateZeroPool (Context.VolumeCount * sizeof (*Context.Rows));
  if (Context.Rows == NULL) {
    SfbReportStatus (L"Out of memory", EFI_OUT_OF_RESOURCES);
    FreePool (Context.Volumes);
    return;
  }
  Context.Mode = Mode;

  for (Index = 0; Index < Context.VolumeCount; Index++) {
    EFI_FILE_PROTOCOL  *Root = NULL;
    CHAR16             Label[SFB_DESC_CHARS];

    Label[0] = L'\0';
    if (!EFI_ERROR (SfbOpenVolumeRoot (Context.Volumes[Index], &Root)) &&
        Root != NULL) {
      SfbGetVolumeLabel (Root, Label, SFB_DESC_CHARS);
      Root->Close (Root);
    }

    /* Distinguish the managed boot root from removable FAT media. */
    if (SfbIsContainerVolume (Context.Volumes[Index])) {
      if (Label[0] != L'\0') {
        if (RETURN_ERROR (StrCatS (Label, SFB_DESC_CHARS, L" (boot root)"))) {
          Label[0] = L'\0';
        }
      } else {
        StrCpyS (Label, SFB_DESC_CHARS, L"boot root");
      }
    }

    if (Label[0] == L'\0') {
      UnicodeSPrint (Context.Rows[Index].Label,
                     sizeof (Context.Rows[Index].Label),
                     L"Volume %u", (UINT32)Index);
    } else {
      UnicodeSPrint (Context.Rows[Index].Label,
                     sizeof (Context.Rows[Index].Label),
                     L"Volume %u: %s", (UINT32)Index, Label);
    }
  }

  Template.Title = L"EFI Program Selector";
  Template.Subtitle = L"Choose a volume to browse.";
  Template.Footer = L"Vol Up/Down: move   Power: select";
  Template.RowCount = Context.VolumeCount + 1;
  Template.Navigate = TRUE;
  Template.Context = &Context;
  Template.Exit = SfbExitProgramMenu;
  Template.Handler = SfbHandleProgramMenuRow;
  Template.DrawRow = SfbDrawProgramMenuRow;
  (VOID)SfbRunMenu (&Template);
}


typedef struct {
  EFI_HANDLE     Volume;
  CHAR16         ToolsPath[SFB_PATH_CHARS];
  SFB_BOOT_MODE  Mode;
} SFB_TOOLS_MENU_CONTEXT;

STATIC
SFB_MENU_ACTION
SfbHandleToolsMenuRow (IN VOID *Context,
                       IN UINTN Row,
                       IN SFB_KEY Key)
{
  SFB_TOOLS_MENU_CONTEXT *State = (SFB_TOOLS_MENU_CONTEXT *)Context;

  (VOID)Key;
  if (Row != 0) {
    return SfbMenuActionExit;
  }
  if (SfbBrowseVolume (State->Volume, L"Android EFI tools", State->ToolsPath,
                       State->Mode)) {
    return SfbMenuActionExit;
  }
  return SfbMenuActionContinue;
}

VOID
SfbRunToolsBrowser (IN SFB_BOOT_MODE Mode)
{
  EFI_STATUS               Status;
  EFI_STATUS               ProbeError = EFI_SUCCESS;
  EFI_HANDLE              *Volumes = NULL;
  UINTN                    VolumeCount = 0;
  UINTN                    Index;
  SFB_TOOLS_MENU_CONTEXT   Context;
  SFB_MENU_TEMPLATE        Template;
  STATIC SFB_MENU_ROW      Rows[] = {
    { L"Browse EFI tools", L" " },
    { L"Back", L" " }
  };

  ZeroMem (&Context, sizeof (Context));
  ZeroMem (&Template, sizeof (Template));

  /* Media may have been inserted since the loader started. */
  SfbStartFatStack ();
  Status = SfbLocateVolumes (&Volumes, &VolumeCount);
  if (EFI_ERROR (Status) || Volumes == NULL) {
    SfbReportStatus (L"No EFI tools installed",
                     EFI_ERROR (Status) ? Status : EFI_NOT_FOUND);
    return;
  }

  for (Index = 0; Index < VolumeCount; Index++) {
    EFI_FILE_PROTOCOL  *Root = NULL;
    EFI_FILE_PROTOCOL  *Dir = NULL;

    /* The managed FAT container exposes tools directly at its root. A path
     * prefix no longer identifies this volume; ordinary FAT roots share it. */
    if (!SfbIsContainerVolume (Volumes[Index])) { continue; }
    StrCpyS (Context.ToolsPath, SFB_PATH_CHARS, L"\\");
    if (EFI_ERROR (SfbJoinPath (Context.ToolsPath, SFB_PATH_CHARS,
                                SFB_TOOLS_DIR_NAME))) { continue; }

    /* Probe before browsing: an absent directory must read as "nothing is
     * installed", not as the browse loop's "cannot read directory". */
    Status = SfbOpenDirectory (Volumes[Index], Context.ToolsPath, &Root, &Dir);
    if (EFI_ERROR (Status)) {
      if (Status != EFI_NOT_FOUND && !EFI_ERROR (ProbeError)) { ProbeError = Status; }
      continue;
    }
    if (Dir != Root) {
      Dir->Close (Dir);
    }
    Root->Close (Root);
    Context.Volume = Volumes[Index];
    Context.Mode = Mode;
    Template.Title = L"Android EFI tools";
    Template.Subtitle = L"Choose an action.";
    Template.Footer = L"Vol Up/Down: move   Power: select";
    Template.Rows = Rows;
    Template.RowCount = ARRAY_SIZE (Rows);
    Template.Navigate = TRUE;
    Template.Context = &Context;
    Template.Handler = SfbHandleToolsMenuRow;
    (VOID)SfbRunMenu (&Template);
    FreePool (Volumes);
    return;
  }

  FreePool (Volumes);
  SfbReportStatus (EFI_ERROR (ProbeError) ? L"Cannot read Android EFI tools" : L"No EFI tools installed",
                   EFI_ERROR (ProbeError) ? ProbeError : EFI_NOT_FOUND);
}
