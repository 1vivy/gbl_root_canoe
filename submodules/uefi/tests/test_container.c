/* Lifecycle contract using production container and Block I/O code. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbContainer.h"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbImageDisk.h"
#include <Library/DevicePathLib.h>
#include <Library/UefiBootServicesTableLib.h>
static EFI_BOOT_SERVICES Services;
EFI_BOOT_SERVICES *gBS = &Services;
EFI_GUID gEfiBlockIoProtocolGuid = {1, 0, 0, {0}}, gEfiDevicePathProtocolGuid = {2, 0, 0, {0}},
         gEfiSimpleFileSystemProtocolGuid = {3, 0, 0, {0}};
static EFI_HANDLE Persist = (void *)1, Virtual = (void *)2;
static EFI_BLOCK_IO_MEDIA Media;
static EFI_BLOCK_IO_PROTOCOL Parent, *Published;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL Fs;
static EFI_FILE_PROTOCOL Root, File;
static EFI_DEVICE_PATH_PROTOCOL Path;
static BOOLEAN Connected, Busy, ParentBusy, FailFlush, Missing, BadFat;
static UINTN Maps, Opens, ParentDisconnects;
static UINT8 Bytes[128 * 1024];
VOID *EFIAPI CopyMem (VOID *a, CONST VOID *b, UINTN n) { return memcpy (a, b, n); }
INTN EFIAPI CompareMem (CONST VOID *a, CONST VOID *b, UINTN n) { return memcmp (a, b, n); }
VOID *EFIAPI ZeroMem (VOID *a, UINTN n) { return memset (a, 0, n); }
VOID *EFIAPI AllocatePool (UINTN n) { return malloc (n); }
VOID *EFIAPI AllocateZeroPool (UINTN n) { return calloc (1, n); }
VOID EFIAPI FreePool (VOID *p) { free (p); }
VOID *EFIAPI AllocateAlignedPages (UINTN n, UINTN a)
{
  return aligned_alloc (a, ((n * 4096 + a - 1) / a) * a);
}
VOID EFIAPI FreeAlignedPages (VOID *p, UINTN n)
{
  (void)n;
  free (p);
}
UINT16 EFIAPI SetDevicePathNodeLength (VOID *n, UINTN len)
{
  EFI_DEVICE_PATH_PROTOCOL *p = n;
  p->Length[0] = len & 255;
  p->Length[1] = len >> 8;
  return (UINT16)len;
}
EFI_DEVICE_PATH_PROTOCOL *EFIAPI DevicePathFromHandle (EFI_HANDLE h)
{
  assert (h == Persist);
  return &Path;
}
EFI_DEVICE_PATH_PROTOCOL *EFIAPI AppendDevicePathNode (CONST EFI_DEVICE_PATH_PROTOCOL *p,
                                                       CONST EFI_DEVICE_PATH_PROTOCOL *n)
{
  (void)p;
  (void)n;
  return calloc (1, 64);
}
static EFI_STATUS EFIAPI read_blocks (EFI_BLOCK_IO_PROTOCOL *p, UINT32 id, EFI_LBA lba, UINTN n,
                                      VOID *out)
{
  assert (p == &Parent && id == 5 && (lba + 1) * n <= sizeof (Bytes) && n == 4096);
  memcpy (out, Bytes + lba * n, n);
  if (BadFat)
    ((UINT8 *)out)[510] = 0;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI write_blocks (EFI_BLOCK_IO_PROTOCOL *p, UINT32 id, EFI_LBA lba, UINTN n,
                                       VOID *in)
{
  (void)p;
  (void)id;
  (void)lba;
  (void)n;
  (void)in;
  assert (!"mount preflight must not write");
  return EFI_DEVICE_ERROR;
}
static EFI_STATUS EFIAPI flush (EFI_BLOCK_IO_PROTOCOL *p)
{
  assert (p == &Parent);
  return FailFlush ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
EFI_STATUS SfbFindPartitionByName (CONST CHAR16 *name, EFI_BLOCK_IO_PROTOCOL **out)
{
  assert (name[0] == 'p');
  *out = &Parent;
  return EFI_SUCCESS;
}
EFI_STATUS Ext4MapImage (EFI_FILE_PROTOCOL *f, EXT4_IMAGE_MAP **out)
{
  assert (f == &File);
  Maps++;
  *out = calloc (1, sizeof (**out));
  (*out)->Parent = &Parent;
  (*out)->MediaId = 5;
  (*out)->Count = 1;
  for (UINTN I = 0; I < 24; I++) (*out)->Identity[I] = (UINT8)I;
  (*out)->Ranges[0] = (EXT4_IMAGE_RANGE){0, 0, EXT4_IMAGE_BYTES};
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI close_file (EFI_FILE_PROTOCOL *f)
{
  assert (f == &File || f == &Root);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI open_file (EFI_FILE_PROTOCOL *r, EFI_FILE_PROTOCOL **f, CHAR16 *name,
                                    UINT64 mode, UINT64 attr)
{
  static CONST CHAR16 wanted[] = L"\\efisp.fat";
  assert (r == &Root && mode == EFI_FILE_MODE_READ && attr == 0);
  assert (memcmp (name, wanted, sizeof (wanted)) == 0);
  Opens++;
  if (Missing)
    return EFI_NOT_FOUND;
  *f = &File;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI open_volume (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs, EFI_FILE_PROTOCOL **r)
{
  assert (fs == &Fs);
  *r = &Root;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI locate (EFI_LOCATE_SEARCH_TYPE t, EFI_GUID *g, VOID *key, UINTN *count,
                                 EFI_HANDLE **out)
{
  assert (t == ByProtocol && g == &gEfiBlockIoProtocolGuid && key == NULL);
  *count = 1;
  *out = malloc (sizeof (**out));
  (*out)[0] = Persist;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI protocol (EFI_HANDLE h, EFI_GUID *g, VOID **out)
{
  *out = NULL;
  if (h == Persist && g == &gEfiBlockIoProtocolGuid)
    *out = &Parent;
  else if (g == &gEfiSimpleFileSystemProtocolGuid && (h == Persist || (h == Virtual && Connected)))
    *out = &Fs;
  return *out ? EFI_SUCCESS : EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI connect (EFI_HANDLE h, EFI_HANDLE *drivers, EFI_DEVICE_PATH_PROTOCOL *path,
                                  BOOLEAN recursive)
{
  (void)drivers;
  (void)path;
  assert (recursive);
  if (h == Virtual)
    Connected = TRUE;
  else
    assert (h == Persist);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI disconnect (EFI_HANDLE h, EFI_HANDLE driver, EFI_HANDLE child)
{
  assert (driver == NULL && child == NULL);
  if (h == Virtual)
  {
    if (Busy)
      return EFI_ACCESS_DENIED;
    Connected = FALSE;
  }
  else
  {
    assert (h == Persist);
    ParentDisconnects++;
    if (ParentBusy)
      return EFI_ACCESS_DENIED;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI install (EFI_HANDLE *h, ...)
{
  va_list a;
  EFI_GUID *g;
  *h = Virtual;
  va_start (a, h);
  while ((g = va_arg (a, EFI_GUID *)) != NULL)
  {
    VOID *v = va_arg (a, VOID *);
    if (g == &gEfiBlockIoProtocolGuid)
      Published = v;
  }
  va_end (a);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI uninstall (EFI_HANDLE h, ...)
{
  assert (h == Virtual && !Connected);
  Published = NULL;
  return EFI_SUCCESS;
}
int main (void)
{
  Services.LocateHandleBuffer = locate;
  Services.HandleProtocol = protocol;
  Services.ConnectController = connect;
  Services.DisconnectController = disconnect;
  Services.InstallMultipleProtocolInterfaces = install;
  Services.UninstallMultipleProtocolInterfaces = uninstall;
  Media.MediaPresent = TRUE;
  Media.MediaId = 5;
  Media.BlockSize = 4096;
  Media.LastBlock = EXT4_IMAGE_BYTES / 4096 - 1;
  Parent.Media = &Media;
  Parent.ReadBlocks = read_blocks;
  Parent.WriteBlocks = write_blocks;
  Parent.FlushBlocks = flush;
  Fs.OpenVolume = open_volume;
  Root.Open = open_file;
  Root.Close = close_file;
  File.Close = close_file;
  Bytes[12] = 2;
  Bytes[13] = 4;
  Bytes[14] = 1;
  Bytes[16] = 2;
  Bytes[18] = 2;
  Bytes[22] = 64;
  Bytes[34] = 1;
  Bytes[510] = 0x55;
  Bytes[511] = 0xaa;
  Bytes[512] = 0xf8;
  Bytes[513] = 255;
  Bytes[514] = 255;
  Bytes[515] = 255;
  memcpy (Bytes + 65 * 512, Bytes + 512, 64 * 512);
  Missing = TRUE;
  assert (SfbContainerMount () == EFI_NOT_FOUND);
  assert (Opens == 1 && Maps == 0 && !Published);
  Missing = FALSE;
  BadFat = TRUE;
  assert (SfbContainerMount () == EFI_VOLUME_CORRUPTED);
  assert (!Published);
  BadFat = FALSE;
  Bytes[65 * 512 + 32] = 1;
  assert (SfbContainerMount () == EFI_VOLUME_CORRUPTED);
  assert (!Published);
  Bytes[65 * 512 + 32] = 0;
  Bytes[515] = 0x3f;
  assert (SfbContainerMount () == EFI_VOLUME_CORRUPTED);
  assert (!Published);
  Bytes[515] = 0xff;
  assert (SfbContainerMount () == EFI_SUCCESS);
  assert (SfbIsContainerVolume (Virtual) && Connected && Published);
  assert (SfbContainerMatchesIdentity ("AAECAwQFBgcICQoLDA0ODxAREhMUFRYX"));
  assert (!SfbContainerMatchesIdentity ("AAECAwQFBgcICQoLDA0ODxAREhMUFRYA"));
  assert (!SfbContainerMatchesIdentity ("AAECAwQFBgcICQoLDA0ODxAREhMUFRY"));
  assert (!SfbContainerMatchesIdentity ("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXx"));
  {
    UINTN before = Maps;
    assert (SfbContainerMount () == EFI_SUCCESS);
    assert (Maps == before);
  }
  Busy = TRUE;
  assert (SfbContainerUnmount () == EFI_ACCESS_DENIED);
  assert (Published && SfbIsContainerVolume (Virtual));
  Busy = FALSE;
  FailFlush = TRUE;
  assert (SfbContainerUnmount () == EFI_DEVICE_ERROR);
  assert (Published && !Connected);
  FailFlush = FALSE;
  assert (SfbContainerMount () == EFI_SUCCESS);
  assert (Connected);
  assert (SfbContainerUnmount () == EFI_SUCCESS);
  assert (!Published && !SfbIsContainerVolume (Virtual) && ParentDisconnects >= 3);
  assert (SfbContainerUnmount () == EFI_SUCCESS);
  {
    EFI_BLOCK_IO_PROTOCOL *Usb = NULL;
    assert (SfbContainerUsbBegin (&Usb) == EFI_SUCCESS);
    assert (Usb != NULL && !Published && !Connected);
    assert (!SfbContainerMatchesIdentity ("AAECAwQFBgcICQoLDA0ODxAREhMUFRYX"));
    assert (SfbContainerDisplayDisk () == NULL);
    assert (SfbContainerMount () == EFI_ACCESS_DENIED);
    assert (SfbContainerUnmount () == EFI_ACCESS_DENIED);
    assert (SfbContainerUsbEnd (FALSE) == EFI_ACCESS_DENIED);
    assert (Usb->FlushBlocks (Usb) == EFI_SUCCESS);
    FailFlush = TRUE;
    assert (SfbContainerUsbEnd (TRUE) == EFI_DEVICE_ERROR);
    assert (SfbContainerMount () == EFI_ACCESS_DENIED);
    FailFlush = FALSE;
    ParentBusy = TRUE;
    assert (SfbContainerUsbEnd (TRUE) == EFI_ACCESS_DENIED);
    /* The virtual disk has already been freed. Retain ownership until the
     * parent's ext4 cache is released; retry must not touch the freed map. */
    assert (SfbContainerMount () == EFI_ACCESS_DENIED);
    assert (SfbContainerUnmount () == EFI_ACCESS_DENIED);
    assert (SfbContainerDisplayDisk () == NULL);
    ParentBusy = FALSE;
    assert (SfbContainerUsbEnd (TRUE) == EFI_SUCCESS);
    assert (SfbContainerMount () == EFI_SUCCESS);
    assert (SfbContainerUnmount () == EFI_SUCCESS);
  }
  puts ("PASS container lifecycle: exact path, invalid FAT, reuse, busy/flush handoff refusal, "
        "retry and cache teardown");
  return 0;
}
