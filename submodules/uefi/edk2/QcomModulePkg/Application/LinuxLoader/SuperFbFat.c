/*
 * Embedded FAT stack for the super-fastboot boot menu.
 *
 * EnhancedFatDxe, DiskIoDxe and the English Unicode Collation driver are linked
 * into this application as static libraries. Their entry points are invoked
 * here by hand rather than by the DXE dispatcher, then a connection pass lets
 * them bind to whatever Block I/O handles the platform published.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbFatClassify.h"
#include "SuperFbConfig.h"
#include "SuperFbFileWindow.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DevicePathLib.h>
#include <Guid/Gpt.h>
#include <Guid/FileInfo.h>
#include <Guid/FileSystemVolumeLabelInfo.h>
#include <IndustryStandard/PeImage.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/PciIo.h>
#include <Protocol/Usb2HostController.h>

/* Keeps the translation unit legal when the feature is compiled out. */
CONST CHAR8 *gSfbFatModuleTag = "SuperFbFat";

/*
 * Entry points of the statically linked drivers. Declared locally because the
 * headers that carry them are module-private to their own packages.
 */
EFI_STATUS
EFIAPI
InitializeUnicodeCollationEng (IN EFI_HANDLE       ImageHandle,
                               IN EFI_SYSTEM_TABLE *SystemTable);

EFI_STATUS
EFIAPI
InitializeDiskIo (IN EFI_HANDLE       ImageHandle,
                  IN EFI_SYSTEM_TABLE *SystemTable);

EFI_STATUS
EFIAPI
FatEntryPoint (IN EFI_HANDLE       ImageHandle,
               IN EFI_SYSTEM_TABLE *SystemTable);

EFI_STATUS
EFIAPI
Ext4EntryPoint (IN EFI_HANDLE       ImageHandle,
                IN EFI_SYSTEM_TABLE *SystemTable);

/*
 * Both driver entry points install their EFI_DRIVER_BINDING_PROTOCOL onto the
 * handle they are handed, and a handle can only carry one of those. They also
 * ASSERT on failure, and this product builds with ASSERT_DEADLOOP enabled, so
 * each driver has to be given a private handle of its own.
 */
STATIC EFI_GUID mSfbDriverTagGuid = {
  0x7b41c0de, 0x2f95, 0x4a18,
  { 0x9c, 0x6d, 0x3e, 0x08, 0xb7, 0x52, 0xd1, 0x64 }
};

STATIC BOOLEAN mSfbFatStackStarted = FALSE;

STATIC
EFI_STATUS
SfbCreateDriverHandle (OUT EFI_HANDLE *Handle)
{
  *Handle = NULL;
  return gBS->InstallProtocolInterface (Handle,
                                        &mSfbDriverTagGuid,
                                        EFI_NATIVE_INTERFACE,
                                        NULL);
}

/*
 * Recursively connect every controller in the system.
 *
 * The fastboot-only boot path skips the BDS "connect all" pass, so on this
 * platform whole device stacks are left dispatched-but-unconnected. Most of
 * them do not matter here, but the USB host storage chain does: this platform's
 * firmware carries the Qualcomm USB host bring-up (UsbConfigDxe), the XHCI
 * PCI-emulation shim, XhciDxe, UsbBusDxe and UsbMassStorageDxe, but nothing in
 * the fastboot path ever connects them, so an attached USB drive never appears.
 */
VOID
SfbConnectAll (VOID)
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count = 0;
  UINTN       Index;
  UINTN       Connected = 0;

  Status = gBS->LocateHandleBuffer (AllHandles, NULL, NULL, &Count, &Handles);
  if (EFI_ERROR (Status) || Handles == NULL) {
    DEBUG ((EFI_D_ERROR, "SFB: no handles to connect: %r\n", Status));
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    Status = gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
    if (!EFI_ERROR (Status)) {
      Connected++;
    }
  }

  DEBUG ((EFI_D_INFO, "SFB: connected %u of %u handles\n",
          (UINT32)Connected, (UINT32)Count));

  FreePool (Handles);
}

EFI_STATUS
SfbStartFatStack (VOID)
{
  EFI_STATUS  Status;
  EFI_HANDLE  DiskIoHandle = NULL;
  EFI_HANDLE  FatHandle = NULL;

  if (mSfbFatStackStarted) {
    /* Re-run the connection pass only: media may have appeared since, and a USB
     * drive may have just been inserted (or a host cable attached). */
    SfbConnectAll ();
    return EFI_SUCCESS;
  }

  /*
   * EnhancedFatDxe refuses to mount a volume without a Unicode Collation
   * producer. This installs onto a handle of its own making, and a second
   * producer alongside a platform-supplied one is harmless: the FAT driver
   * picks whichever matches the platform language.
   */
  Status = InitializeUnicodeCollationEng (gImageHandle, gST);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: Unicode Collation init failed: %r\n", Status));
    return Status;
  }

  Status = SfbCreateDriverHandle (&DiskIoHandle);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: Disk I/O handle alloc failed: %r\n", Status));
    return Status;
  }

  Status = InitializeDiskIo (DiskIoHandle, gST);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: Disk I/O driver init failed: %r\n", Status));
    return Status;
  }

  Status = SfbCreateDriverHandle (&FatHandle);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: FAT handle alloc failed: %r\n", Status));
    return Status;
  }

  Status = FatEntryPoint (FatHandle, gST);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: FAT driver init failed: %r\n", Status));
    return Status;
  }

  /*
   * The read-only EXT4 driver mounts the ext4 persist partition so its \efisp
   * directory can be scanned and browsed like a FAT32 volume. Same pattern as
   * FAT above: a private handle carries its driver binding, and the connect
   * pass at the end binds it to the Disk I/O handles of any ext4 partitions.
   * Failure here is non-fatal to the FAT stack already up, but the persist
   * volume simply will not appear.
   */
  Status = SfbCreateDriverHandle (&FatHandle);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SFB: Ext4 handle alloc failed: %r\n", Status));
  } else {
    Status = Ext4EntryPoint (FatHandle, gST);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "SFB: Ext4 driver init failed: %r\n", Status));
    }
  }

  mSfbFatStackStarted = TRUE;

  SfbConnectAll ();

  return EFI_SUCCESS;
}

/*
 * Bind the writable configuration store.
 *
 * persist is ext4 and its driver is read-only by construction, so mutable menu
 * state cannot be an ordinary file there. `\efisp.fat` on that volume is a
 * fixed-extent image whose physical blocks are republished as a block window;
 * the FAT driver already linked into this application binds to that window and
 * what comes out is an ordinary writable volume. Fails soft by design: with no
 * image, an unmappable one, or one the FAT driver declines, nothing is bound and
 * the store keeps answering from the legacy records in the efisp tail.
 */
VOID
SfbBindWritableStore (VOID)
{
  EFI_STATUS            Status;
  EFI_HANDLE            *Volumes = NULL;
  UINTN                 Count = 0;
  UINTN                 Index;
  EFI_HANDLE            Window = NULL;
  SFB_FILE_WINDOW_INFO  Info;
  VOID                  *FileSystem = NULL;

  ZeroMem (&Info, sizeof (Info));
  Status = SfbLocateVolumes (&Volumes, &Count);
  if (EFI_ERROR (Status) || Volumes == NULL) {
    DEBUG ((EFI_D_INFO,
            "SFB: MARK store-bind status=%r runs=%u bytes=%Lu stamp=%u\n",
            Status, 0U, 0ULL, 0U));
    if (Volumes != NULL) {
      FreePool (Volumes);
    }
    return;
  }

  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < Count; Index++) {
    if (!SfbVolumeIsExt4 (Volumes[Index])) {
      continue;
    }
    ZeroMem (&Info, sizeof (Info));
    Status = SfbOpenFileWindow (Volumes[Index], L"\\efisp.fat", &Window, &Info);
    if (EFI_ERROR (Status)) {
      continue;
    }
    /* The window publishes Block I/O only; Disk I/O and FAT bind on connect. */
    SfbConnectAll ();
    Status = gBS->HandleProtocol (Window, &gEfiSimpleFileSystemProtocolGuid,
                                  &FileSystem);
    if (EFI_ERROR (Status) || FileSystem == NULL) {
      SfbCloseFileWindow (Window);
      Window = NULL;
      ZeroMem (&Info, sizeof (Info));
      continue;
    }
    SfbConfigBindVolume (Window);
    Status = EFI_SUCCESS;
    break;
  }

  DEBUG ((EFI_D_INFO,
          "SFB: MARK store-bind status=%r runs=%u bytes=%Lu stamp=%u\n",
          Status, (UINT32)Info.RunCount, Info.VolumeBytes,
          (UINT32)Info.StampValid));
  FreePool (Volumes);
}

/* ---- logfs -------------------------------------------------------------- */

extern EFI_GUID gEfiPartitionRecordGuid;

/* GPT name fields are CHAR16[36], not required to be NUL-terminated and
 * space-padded by some writers. Case-insensitive, like the reserve table's
 * matcher in Hook/BlockIoHook.c. */
STATIC BOOLEAN
SfbGptNameIsLogfs (IN CONST CHAR16 *Stored)
{
  STATIC CONST CHAR16  Want[] = L"logfs";
  UINTN                Index;

  if (Stored == NULL) return FALSE;
  for (Index = 0; Index < 36 && Want[Index] != L'\0'; Index++) {
    CHAR16 A = Stored[Index];
    CHAR16 B = Want[Index];

    if (A >= L'A' && A <= L'Z') A = (CHAR16)(A | 0x20);
    if (A != B) return FALSE;
  }
  return Stored[Index] == L'\0' || Stored[Index] == L' ';
}

STATIC UINTN
SfbFileSystemCount (VOID)
{
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count  = 0;

  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol,
                                           &gEfiSimpleFileSystemProtocolGuid,
                                           NULL, &Count, &Handles)) &&
      Handles != NULL) {
    FreePool (Handles);
  }
  return Count;
}

/*
 * Mount the logfs partition, nothing more.
 *
 * The Qualcomm BDS earlier in the boot chain defers its log flush until logfs
 * is mounted, and the stock fastboot path never mounts it, so the buffered log
 * is silently dropped. Binding the filesystem drivers to that one partition is
 * all it takes; the flush itself is Qualcomm's business. Fail-soft by
 * construction: a platform with no logfs partition mounts nothing, and the
 * marker records that as an expected outcome rather than a failure.
 */
VOID
SfbMountLogfs (VOID)
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count    = 0;
  UINTN       Index;
  UINTN       Found    = 0;
  UINTN       Mounted  = 0;

  if (!mSfbFatStackStarted) {
    return;
  }

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid,
                                    NULL, &Count, &Handles);
  if (EFI_ERROR (Status) || Handles == NULL) {
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    EFI_PARTITION_ENTRY  *PartEntry = NULL;
    UINTN                Before;
    UINTN                After;

    Status = gBS->HandleProtocol (Handles[Index], &gEfiPartitionRecordGuid,
                                  (VOID **)&PartEntry);
    if (EFI_ERROR (Status) || PartEntry == NULL) continue;
    if (!SfbGptNameIsLogfs (PartEntry->PartitionName)) continue;

    Found++;
    Before = SfbFileSystemCount ();
    gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
    After = SfbFileSystemCount ();
    if (After > Before) Mounted++;
  }

  FreePool (Handles);
  DEBUG ((EFI_D_INFO, "SFB: MARK logfs-mount found=%u mounted=%u\n",
          (UINT32)Found, (UINT32)Mounted));
}

/*
 * Little-endian helpers used by the PE driver-file probe below. Volume
 * classification itself lives in SuperFbFatClassify.h so the same pure
 * decision is used by firmware and host regressions.
 */

STATIC
UINT16
SfbLe16 (IN CONST UINT8 *Sector, IN UINTN Offset)
{
  return (UINT16)(Sector[Offset] | ((UINT16)Sector[Offset + 1] << 8));
}

STATIC
UINT32
SfbLe32 (IN CONST UINT8 *Sector, IN UINTN Offset)
{
  return (UINT32)Sector[Offset] |
         ((UINT32)Sector[Offset + 1] << 8) |
         ((UINT32)Sector[Offset + 2] << 16) |
         ((UINT32)Sector[Offset + 3] << 24);
}

/*
 * Decide from the boot sector alone. The FAT type is defined by the geometry
 * rather than by the "FAT32" text at offset 82, which is documented as
 * informational only.
 */
BOOLEAN
SfbIsFat32Volume (IN EFI_HANDLE Volume)
{
  EFI_STATUS             Status;
  EFI_BLOCK_IO_PROTOCOL  *BlockIo = NULL;
  UINT8                  *Sector;
  UINTN                  SectorSize;
  BOOLEAN                IsFat32 = FALSE;

  Status = gBS->HandleProtocol (Volume, &gEfiBlockIoProtocolGuid,
                                (VOID **)&BlockIo);
  if (EFI_ERROR (Status) || BlockIo == NULL || BlockIo->Media == NULL) {
    /* No block device behind it: this is not a partition at all. */
    return FALSE;
  }

  if (!BlockIo->Media->MediaPresent) {
    return FALSE;
  }

  SectorSize = BlockIo->Media->BlockSize;
  if (SectorSize < 512) {
    return FALSE;
  }

  Sector = AllocateAlignedPages (EFI_SIZE_TO_PAGES (SectorSize),
                                 BlockIo->Media->IoAlign > 1 ?
                                   BlockIo->Media->IoAlign : 8);
  if (Sector == NULL) {
    return FALSE;
  }

  Status = BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId, 0,
                                SectorSize, Sector);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_VERBOSE, "SFB: boot sector read failed: %r\n", Status));
  } else {
    IsFat32 = (BOOLEAN)(SfbClassifyVolumeBytes (Sector, SectorSize) ==
                        SfbVolumeKindFat32);
  }

  FreeAlignedPages (Sector, EFI_SIZE_TO_PAGES (SectorSize));
  return IsFat32;
}

/*
 * The ext4 superblock sits 1024 bytes into the partition and carries the
 * 0xEF53 signature at offset 56 within it (byte 1080). The shared classifier
 * checks FAT32 geometry first and validates the ext4 layout before accepting
 * the magic, so a coincidental match cannot redirect a FAT32 root.
 */
BOOLEAN
SfbIsExt4Volume (IN EFI_HANDLE Volume)
{
  EFI_STATUS             Status;
  EFI_BLOCK_IO_PROTOCOL  *BlockIo = NULL;
  UINT8                  *Sector;
  UINTN                  SectorSize;
  UINTN                  Bytes;
  UINTN                  Blocks;
  BOOLEAN                IsExt4 = FALSE;
  Status = gBS->HandleProtocol (Volume, &gEfiBlockIoProtocolGuid,
                                (VOID **)&BlockIo);
  if (EFI_ERROR (Status) || BlockIo == NULL || BlockIo->Media == NULL) {
    return FALSE;
  }

  if (!BlockIo->Media->MediaPresent) {
    return FALSE;
  }

  SectorSize = BlockIo->Media->BlockSize;
  if (SectorSize < 512) {
    return FALSE;
  }

  /* Read enough to cover the ext4 superblock; the classifier validates all
   * fields before accepting the magic. */
  Bytes = 4096;
  Blocks = (Bytes + SectorSize - 1) / SectorSize;
  Bytes = Blocks * SectorSize;

  Sector = AllocateAlignedPages (EFI_SIZE_TO_PAGES (Bytes),
                                 BlockIo->Media->IoAlign > 1 ?
                                   BlockIo->Media->IoAlign : 8);
  if (Sector == NULL) {
    return FALSE;
  }

  Status = BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId, 0,
                                Bytes, Sector);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_VERBOSE, "SFB: ext4 superblock read failed: %r\n", Status));
  } else {
    IsExt4 = (BOOLEAN)(SfbClassifyVolumeBytes (Sector, Bytes) ==
                       SfbVolumeKindExt4);
  }

  FreeAlignedPages (Sector, EFI_SIZE_TO_PAGES (Bytes));
  return IsExt4;
}


typedef struct {
  EFI_HANDLE       Volume;
  SFB_VOLUME_KIND  Kind;
} SFB_VOLUME_CLASS;

#define SFB_VOLUME_CLASS_CACHE_MAX  256

STATIC SFB_VOLUME_CLASS  mSfbVolumeClassCache[SFB_VOLUME_CLASS_CACHE_MAX];
STATIC UINTN             mSfbVolumeClassCount;

STATIC
VOID
SfbResetVolumeClassCache (VOID)
{
  ZeroMem (mSfbVolumeClassCache, sizeof (mSfbVolumeClassCache));
  mSfbVolumeClassCount = 0;
}

STATIC
SFB_VOLUME_KIND
SfbProbeVolumeKind (IN EFI_HANDLE Volume)
{
  EFI_STATUS             Status;
  EFI_BLOCK_IO_PROTOCOL  *BlockIo = NULL;
  UINT8                  *Sector;
  UINTN                  SectorSize;
  UINTN                  Bytes;
  UINTN                  Blocks;
  SFB_VOLUME_KIND        Kind = SfbVolumeKindOther;

  Status = gBS->HandleProtocol (Volume, &gEfiBlockIoProtocolGuid,
                                (VOID **)&BlockIo);
  if (EFI_ERROR (Status) || BlockIo == NULL || BlockIo->Media == NULL ||
      !BlockIo->Media->MediaPresent) {
    return Kind;
  }

  SectorSize = BlockIo->Media->BlockSize;
  if (SectorSize < 512) {
    return Kind;
  }

  Bytes = 4096;
  Blocks = (Bytes + SectorSize - 1) / SectorSize;
  Bytes = Blocks * SectorSize;
  Sector = AllocateAlignedPages (EFI_SIZE_TO_PAGES (Bytes),
                                 BlockIo->Media->IoAlign > 1 ?
                                   BlockIo->Media->IoAlign : 8);
  if (Sector == NULL) {
    return Kind;
  }

  Status = BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId, 0,
                                Bytes, Sector);
  if (!EFI_ERROR (Status)) {
    Kind = SfbClassifyVolumeBytes (Sector, Bytes);
  }
  FreeAlignedPages (Sector, EFI_SIZE_TO_PAGES (Bytes));
  return Kind;
}

STATIC
SFB_VOLUME_KIND
SfbClassifyVolume (IN EFI_HANDLE Volume)
{
  UINTN           Index;
  SFB_VOLUME_KIND Kind;

  for (Index = 0; Index < mSfbVolumeClassCount; Index++) {
    if (mSfbVolumeClassCache[Index].Volume == Volume) {
      return mSfbVolumeClassCache[Index].Kind;
    }
  }

  Kind = SfbProbeVolumeKind (Volume);
  if (mSfbVolumeClassCount < SFB_VOLUME_CLASS_CACHE_MAX) {
    mSfbVolumeClassCache[mSfbVolumeClassCount].Volume = Volume;
    mSfbVolumeClassCache[mSfbVolumeClassCount].Kind = Kind;
    mSfbVolumeClassCount++;
  } else {
    DEBUG ((EFI_D_WARN, "SFB: volume classification cache full\n"));
    Kind = SfbVolumeKindOther;
  }
  return Kind;
}
/*
 * The subdirectory that plays the role of a FAT32 volume root on a given
 * volume: empty for genuine FAT32 (its root already is the scan root) and
 * \efisp for the ext4 persist partition, whose boot files live there. The
 * entry scanner and the browser prepend this to the well-known boot file
 * paths and use it as the browse floor respectively.
 */
CONST CHAR16 *
SfbVolumeRootPrefix (IN EFI_HANDLE Volume)
{
  return SfbClassifyVolume (Volume) == SfbVolumeKindExt4 ?
           L"\\efisp" : L"";
}
BOOLEAN
SfbVolumeIsExt4 (IN EFI_HANDLE Volume)
{
  return (BOOLEAN)(SfbClassifyVolume (Volume) == SfbVolumeKindExt4);
}

/*
 * TRUE when Path names an existing directory on Volume. Ext4 volumes are only
 * treated as boot volumes when they carry \efisp, so an ext4 partition whose
 * \efisp directory has not been created is never scanned or offered in the
 * browser. FAT32 volumes are never gated on this: their root is the boot root.
 */
STATIC
BOOLEAN
SfbVolumeHasDir (IN EFI_HANDLE Volume, IN CONST CHAR16 *Path)
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Root = NULL;
  EFI_FILE_PROTOCOL  *Dir = NULL;
  EFI_FILE_INFO      *Info;
  UINTN              InfoSize;
  BOOLEAN            IsDir = FALSE;

  if (EFI_ERROR (SfbOpenVolumeRoot (Volume, &Root)) || Root == NULL) {
    return FALSE;
  }

  Status = Root->Open (Root, &Dir, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status) || Dir == NULL) {
    Root->Close (Root);
    return FALSE;
  }

  InfoSize = 0;
  Status = Dir->GetInfo (Dir, &gEfiFileInfoGuid, &InfoSize, NULL);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    Info = AllocateZeroPool (InfoSize);
    if (Info != NULL) {
      Status = Dir->GetInfo (Dir, &gEfiFileInfoGuid, &InfoSize, Info);
      if (!EFI_ERROR (Status)) {
        IsDir = (BOOLEAN)((Info->Attribute & EFI_FILE_DIRECTORY) != 0);
      }
      FreePool (Info);
    }
  }

  Dir->Close (Dir);
  Root->Close (Root);

  return IsDir;
}

EFI_STATUS
SfbLocateVolumes (OUT EFI_HANDLE **Handles, OUT UINTN *Count)
{
  EFI_STATUS        Status;
  EFI_HANDLE        *All = NULL;
  UINTN              AllCount = 0;
  UINTN              Kept = 0;
  UINTN              Index;
  SFB_VOLUME_KIND    Kind;

  *Handles = NULL;
  *Count = 0;
  SfbResetVolumeClassCache ();

  Status = gBS->LocateHandleBuffer (ByProtocol,
                                    &gEfiSimpleFileSystemProtocolGuid,
                                    NULL,
                                    &AllCount,
                                    &All);
  if (EFI_ERROR (Status) || All == NULL) {
    return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;
  }

  /* Filter in place: the buffer is ours, and the survivors keep their order.
   * FAT32 volumes are the menu's traditional boot media; ext4 volumes are the
   * persist partition, whose \efisp directory the scanner treats as a volume
   * root via SfbVolumeRootPrefix (). An ext4 volume without \efisp is dropped:
   * it has no boot root to scan and nothing to browse, so it would only clutter
   * the menu. Anything else is dropped too. */
  for (Index = 0; Index < AllCount; Index++) {
    Kind = SfbClassifyVolume (All[Index]);
    if (Kind == SfbVolumeKindFat32 ||
        (Kind == SfbVolumeKindExt4 &&
         SfbVolumeHasDir (All[Index], L"\\efisp"))) {
      All[Kept++] = All[Index];
    }
  }

  DEBUG ((EFI_D_INFO, "SFB: %u of %u file systems are FAT32/ext4\n",
          (UINT32)Kept, (UINT32)AllCount));

  if (Kept == 0) {
    FreePool (All);
    return EFI_NOT_FOUND;
  }

  *Handles = All;
  *Count = Kept;

  return EFI_SUCCESS;
}

EFI_STATUS
SfbOpenVolumeRoot (IN EFI_HANDLE Volume, OUT EFI_FILE_PROTOCOL **Root)
{
  EFI_STATUS                       Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs = NULL;

  *Root = NULL;

  Status = gBS->HandleProtocol (Volume,
                                &gEfiSimpleFileSystemProtocolGuid,
                                (VOID **)&Fs);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return Fs->OpenVolume (Fs, Root);
}

BOOLEAN
SfbFileExists (IN EFI_FILE_PROTOCOL *Root, IN CONST CHAR16 *Path)
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *File = NULL;
  EFI_FILE_INFO      *Info;
  UINTN              InfoSize;
  BOOLEAN            IsFile = FALSE;

  Status = Root->Open (Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status) || File == NULL) {
    return FALSE;
  }

  InfoSize = 0;
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, NULL);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    Info = AllocateZeroPool (InfoSize);
    if (Info != NULL) {
      Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, Info);
      if (!EFI_ERROR (Status)) {
        IsFile = (BOOLEAN)((Info->Attribute & EFI_FILE_DIRECTORY) == 0);
      }
      FreePool (Info);
    }
  }

  File->Close (File);

  return IsFile;
}

EFI_STATUS
SfbReadFileBytes (IN EFI_FILE_PROTOCOL *Root,
                  IN CONST CHAR16      *Path,
                  OUT VOID             *Buffer,
                  IN UINTN             MaxBytes,
                  OUT UINTN            *BytesRead)
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *File = NULL;
  UINTN              ReadSize = MaxBytes;

  *BytesRead = 0;

  Status = Root->Open (Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status) || File == NULL) {
    return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;
  }

  Status = File->Read (File, &ReadSize, Buffer);
  File->Close (File);

  if (EFI_ERROR (Status)) {
    return Status;
  }

  *BytesRead = ReadSize;

  return EFI_SUCCESS;
}

/*
 * Decide application vs driver from the PE image's subsystem field. The layout
 * checked here is fixed for both PE32 and PE32+: a DOS "MZ" header carries the
 * offset of the PE signature at 0x3C, the COFF header follows the 4-byte
 * signature, and the optional header's 16-bit Subsystem sits 68 bytes into it.
 * Reading a header-sized prefix is enough; anything that does not parse as a PE
 * image is reported as "not a driver" so the caller falls back to app handling.
 */
BOOLEAN
SfbIsEfiDriverFile (IN EFI_FILE_PROTOCOL *Root, IN CONST CHAR16 *Path)
{
  /*
   * Must be large enough to contain the PE optional header's Subsystem field.
   * EDK2/GCC/CLANG images place the PE signature well past the classic 0x40-ish
   * offset (typically e_lfanew ~= 0xE58 for AARCH64 output), so a 512-byte
   * prefix stops short of Subsystem (which lands near file offset 0xEB4) and the
   * bounds check below would wrongly report every such image as "not a driver".
   * 8 KiB comfortably covers e_lfanew for all images this menu launches.
   */
  UINT8   Header[8192];
  UINTN   Read = 0;
  UINT32  PeOffset;
  UINTN   SubsystemAt;
  UINT16  Subsystem;

  if (EFI_ERROR (SfbReadFileBytes (Root, Path, Header, sizeof (Header), &Read))) {
    return FALSE;
  }

  /* Need at least the DOS header and its e_lfanew field. */
  if (Read < 0x40) {
    return FALSE;
  }
  if (SfbLe16 (Header, 0) != EFI_IMAGE_DOS_SIGNATURE) {   /* 'MZ' */
    return FALSE;
  }

  PeOffset = SfbLe32 (Header, 0x3C);

  /* Optional header starts after the 4-byte PE signature and 20-byte COFF
   * header; Subsystem is 68 bytes into it. */
  SubsystemAt = (UINTN)PeOffset + 4 + 20 + 68;
  if (SubsystemAt + sizeof (UINT16) > Read) {
    return FALSE;
  }
  if (SfbLe32 (Header, PeOffset) != EFI_IMAGE_NT_SIGNATURE) {  /* 'PE\0\0' */
    return FALSE;
  }

  Subsystem = SfbLe16 (Header, SubsystemAt);

  return (BOOLEAN)(Subsystem == EFI_IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER ||
                   Subsystem == EFI_IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER);
}

VOID
SfbReadAnsiDescription (IN EFI_FILE_PROTOCOL *Root,
                        IN CONST CHAR16      *Path,
                        OUT CHAR16           *Out,
                        IN UINTN             OutChars)
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *File = NULL;
  CHAR8              Buffer[256];
  UINTN              ReadSize;
  UINTN              Index;

  if (OutChars == 0) {
    return;
  }

  Status = Root->Open (Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status) || File == NULL) {
    return;
  }

  ReadSize = sizeof (Buffer);
  Status = File->Read (File, &ReadSize, Buffer);
  File->Close (File);

  if (EFI_ERROR (Status) || ReadSize == 0) {
    return;
  }

  /* First line only, and only printable 7-bit characters: the console cannot
   * render anything else usefully and the file is specified as ANSI. */
  for (Index = 0; Index < ReadSize && Index < OutChars - 1; Index++) {
    CHAR8 Ch = Buffer[Index];

    if (Ch == '\0' || Ch == '\r' || Ch == '\n') {
      break;
    }
    if (Ch < 0x20 || (UINT8)Ch > 0x7e) {
      Ch = ' ';
    }
    Out[Index] = (CHAR16)Ch;
  }

  /* Leave the caller's fallback in place rather than blanking it. */
  if (Index > 0) {
    Out[Index] = L'\0';
  }
}

VOID
SfbGetVolumeLabel (IN EFI_FILE_PROTOCOL *Root,
                   OUT CHAR16           *Out,
                   IN UINTN             OutChars)
{
  EFI_STATUS                       Status;
  EFI_FILE_SYSTEM_VOLUME_LABEL     *Label;
  UINTN                            InfoSize;

  if (OutChars == 0) {
    return;
  }
  Out[0] = L'\0';

  InfoSize = 0;
  Status = Root->GetInfo (Root,
                          &gEfiFileSystemVolumeLabelInfoIdGuid,
                          &InfoSize,
                          NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return;
  }

  Label = AllocateZeroPool (InfoSize);
  if (Label == NULL) {
    return;
  }

  Status = Root->GetInfo (Root,
                          &gEfiFileSystemVolumeLabelInfoIdGuid,
                          &InfoSize,
                          Label);
  if (!EFI_ERROR (Status)) {
    StrnCpyS (Out, OutChars, Label->VolumeLabel, OutChars - 1);
  }

  FreePool (Label);
}

