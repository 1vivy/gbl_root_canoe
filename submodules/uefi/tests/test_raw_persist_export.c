/* Exercise the production raw-export preparation boundary without a gadget. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#undef NULL
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbMassStorage.c"

static EFI_BOOT_SERVICES Services;
EFI_BOOT_SERVICES *gBS = &Services;
EFI_GUID gEfiBlockIoProtocolGuid = {1, 0, 0, {0}};
EFI_GUID gEfiSimpleFileSystemProtocolGuid = {2, 0, 0, {0}};
static EFI_BLOCK_IO_PROTOCOL PersistDisk, OtherDisk;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL FileSystem;
static EFI_HANDLE Persist = (VOID *)1, Other = (VOID *)2;
static EFI_STATUS LocateStatus, ReleaseStatus, FilesystemStatus;
static BOOLEAN Mounted, Foreign, Missing;
static UINTN Releases, Flushes, Freed;

INTN EFIAPI StrCmp (CONST CHAR16 *A, CONST CHAR16 *B)
{
  while (*A != 0 && *A == *B) { A++; B++; }
  return (INTN)*A - (INTN)*B;
}
VOID EFIAPI FreePool (VOID *Buffer) { Freed++; free (Buffer); }
static EFI_STATUS EFIAPI Locate (EFI_LOCATE_SEARCH_TYPE Type, EFI_GUID *Guid,
                                 VOID *Key, UINTN *Count, EFI_HANDLE **Handles)
{
  (VOID)Key;
  assert (Type == ByProtocol && Guid == &gEfiBlockIoProtocolGuid);
  if (EFI_ERROR (LocateStatus)) return LocateStatus;
  *Handles = malloc (2 * sizeof (**Handles));
  assert (*Handles != NULL);
  (*Handles)[0] = Other;
  (*Handles)[1] = Persist;
  *Count = 2;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Protocol (EFI_HANDLE Handle, EFI_GUID *Guid, VOID **Out)
{
  if (Guid == &gEfiBlockIoProtocolGuid) {
    *Out = Handle == Persist && !Missing ? &PersistDisk : &OtherDisk;
    return EFI_SUCCESS;
  }
  assert (Guid == &gEfiSimpleFileSystemProtocolGuid && Handle == Persist);
  *Out = NULL;
  if (EFI_ERROR (FilesystemStatus)) return FilesystemStatus;
  if (Mounted) { *Out = &FileSystem; return EFI_SUCCESS; }
  return EFI_UNSUPPORTED;
}
EFI_STATUS Ext4ReleaseImageFileSystem (EFI_HANDLE Handle)
{
  assert (Handle == Persist);
  Releases++;
  if (EFI_ERROR (ReleaseStatus)) return ReleaseStatus;
  if (!Foreign) Mounted = FALSE;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Flush (EFI_BLOCK_IO_PROTOCOL *Disk)
{
  if (Disk == &PersistDisk) assert (!Mounted && Releases == 1);
  else assert (Disk == &OtherDisk && Releases == 0);
  Flushes++;
  return EFI_SUCCESS;
}
static void Reset (void)
{
  Services.LocateHandleBuffer = Locate;
  Services.HandleProtocol = Protocol;
  PersistDisk.FlushBlocks = Flush;
  OtherDisk.FlushBlocks = Flush;
  LocateStatus = ReleaseStatus = FilesystemStatus = EFI_SUCCESS;
  Mounted = TRUE; Foreign = Missing = FALSE;
  Releases = Flushes = Freed = 0;
}
int main (void)
{
  /* Ext4 may be mounted without any Container map. Stop it before raw flush. */
  Reset ();
  assert (PrepareRawDisk (L"persist", &PersistDisk) == EFI_SUCCESS);
  assert (Releases == 1 && Flushes == 1 && Freed == 1 && !Mounted);

  Reset (); Mounted = FALSE;
  assert (PrepareRawDisk (L"persist", &PersistDisk) == EFI_SUCCESS);
  assert (Flushes == 1);

  Reset (); ReleaseStatus = EFI_ACCESS_DENIED;
  assert (PrepareRawDisk (L"persist", &PersistDisk) == EFI_ACCESS_DENIED);
  assert (Mounted && Releases == 1 && Flushes == 0 && Freed == 1);

  Reset (); Foreign = TRUE;
  assert (PrepareRawDisk (L"persist", &PersistDisk) == EFI_ACCESS_DENIED);
  assert (Mounted && Flushes == 0);

  Reset (); FilesystemStatus = EFI_DEVICE_ERROR;
  assert (PrepareRawDisk (L"persist", &PersistDisk) == EFI_DEVICE_ERROR);
  assert (Flushes == 0 && Freed == 1);

  Reset (); Missing = TRUE;
  assert (PrepareRawDisk (L"persist", &PersistDisk) == EFI_NOT_FOUND);
  assert (Releases == 0 && Flushes == 0 && Freed == 1);

  Reset (); LocateStatus = EFI_DEVICE_ERROR;
  assert (PrepareRawDisk (L"persist", &PersistDisk) == EFI_DEVICE_ERROR);
  assert (Releases == 0 && Flushes == 0 && Freed == 0);

  /* A different raw partition must not stop persist's filesystem. */
  Reset ();
  assert (PrepareRawDisk (L"logfs", &OtherDisk) == EFI_SUCCESS);
  assert (Mounted && Releases == 0 && Flushes == 1 && Freed == 0);
  puts ("raw persist export preparation: passed");
  return 0;
}
