#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbImageDisk.h"
#define BYTES (64U * 1024U * 1024U)
static unsigned char *Storage;
static EFI_BLOCK_IO_MEDIA Media;
static EFI_BLOCK_IO_PROTOCOL Parent;
static UINTN Reads, Writes;
static BOOLEAN ReadFailure, WriteFailure, FlushFailure;
VOID *EFIAPI CopyMem (VOID *a, CONST VOID *b, UINTN n) { return memcpy (a, b, n); }
VOID *EFIAPI ZeroMem (VOID *a, UINTN n) { return memset (a, 0, n); }
VOID *EFIAPI AllocateAlignedPages (UINTN pages, UINTN alignment)
{
  return aligned_alloc (alignment, ((pages * 4096 + alignment - 1) / alignment) * alignment);
}
VOID EFIAPI FreeAlignedPages (VOID *p, UINTN pages)
{
  (void)pages;
  free (p);
}
static EFI_STATUS EFIAPI read_blocks (EFI_BLOCK_IO_PROTOCOL *p, UINT32 id, EFI_LBA lba, UINTN n,
                                      VOID *out)
{
  assert (p == &Parent && id == Media.MediaId && n == Media.BlockSize &&
          ((UINTN)out % Media.IoAlign) == 0 && (lba + 1) * n <= BYTES);
  Reads++;
  if (ReadFailure)
    return EFI_DEVICE_ERROR;
  memcpy (out, Storage + lba * n, n);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI write_blocks (EFI_BLOCK_IO_PROTOCOL *p, UINT32 id, EFI_LBA lba, UINTN n,
                                       VOID *in)
{
  assert (p == &Parent && id == Media.MediaId && n == Media.BlockSize &&
          ((UINTN)in % Media.IoAlign) == 0 && (lba + 1) * n <= BYTES);
  Writes++;
  if (WriteFailure)
    return EFI_DEVICE_ERROR;
  memcpy (Storage + lba * n, in, n);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI flush (EFI_BLOCK_IO_PROTOCOL *p)
{
  assert (p == &Parent);
  return FlushFailure ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
int main (void)
{
  EXT4_IMAGE_MAP *m = calloc (1, sizeof (*m));
  SFB_IMAGE_DISK d;
  unsigned char data[4096], out[4096], *expected = malloc (BYTES);
  Storage = malloc (BYTES);
  memset (Storage, 0x5a, BYTES);
  memcpy (expected, Storage, BYTES);
  memset (data, 0xc3, sizeof (data));
  Media.MediaPresent = TRUE;
  Media.MediaId = 42;
  Media.BlockSize = 4096;
  Media.IoAlign = 4096;
  Media.LastBlock = BYTES / 4096 - 1;
  Parent.Media = &Media;
  Parent.ReadBlocks = read_blocks;
  Parent.WriteBlocks = write_blocks;
  Parent.FlushBlocks = flush;
  m->Parent = &Parent;
  m->MediaId = 42;
  m->Count = 3;
  m->Ranges[0] = (EXT4_IMAGE_RANGE){0, 1024, 1024};
  m->Ranges[1] = (EXT4_IMAGE_RANGE){1024, 16 * 1024 * 1024 + 1024, 16 * 1024 * 1024};
  m->Ranges[2] =
      (EXT4_IMAGE_RANGE){16 * 1024 * 1024 + 1024, 40 * 1024 * 1024, 16 * 1024 * 1024 - 1024};
  assert (SfbImageDiskInit (&d, m) == EFI_SUCCESS);
  assert (d.Block.WriteBlocks (&d.Block, 42, 1, sizeof (data), data) == EFI_SUCCESS);
  memcpy (expected + 1536, data, 512);
  memcpy (expected + 16 * 1024 * 1024 + 1024, data + 512, sizeof (data) - 512);
  assert (memcmp (Storage, expected, BYTES) ==
          0); /* every byte outside the requested file range survives */
  assert (d.Block.ReadBlocks (&d.Block, 42, 1, sizeof (out), out) == EFI_SUCCESS);
  assert (memcmp (data, out, sizeof (data)) == 0);
  {
    UINTN before = Writes;
    assert (d.Block.WriteBlocks (&d.Block, 42, EXT4_IMAGE_BYTES / 512, 512, data) ==
            EFI_INVALID_PARAMETER);
    assert (d.Block.WriteBlocks (&d.Block, 42, 0, 513, data) == EFI_BAD_BUFFER_SIZE);
    assert (before == Writes);
  }
  ReadFailure = TRUE;
  assert (d.Block.WriteBlocks (&d.Block, 42, 1, 512, data) == EFI_DEVICE_ERROR);
  ReadFailure = FALSE;
  WriteFailure = TRUE;
  assert (d.Block.WriteBlocks (&d.Block, 42, 1, 512, data) == EFI_DEVICE_ERROR);
  WriteFailure = FALSE;
  FlushFailure = TRUE;
  assert (d.Block.FlushBlocks (&d.Block) == EFI_DEVICE_ERROR);
  FlushFailure = FALSE;
  assert (d.Block.FlushBlocks (&d.Block) == EFI_SUCCESS);
  Media.MediaId++;
  assert (d.Block.ReadBlocks (&d.Block, 42, 0, 512, out) == EFI_MEDIA_CHANGED);
  Media.MediaId--;
  d.Active = FALSE;
  assert (d.Block.ReadBlocks (&d.Block, 42, 0, 512, out) == EFI_NO_MEDIA);
  d.Active = TRUE;
  Media.ReadOnly = TRUE;
  assert (d.Block.WriteBlocks (&d.Block, 42, 0, 512, data) == EFI_WRITE_PROTECTED);
  Media.ReadOnly = FALSE;
  SfbImageDiskDestroy (&d);
  assert (d.Block.ReadBlocks (&d.Block, 42, 0, 512, out) == EFI_NO_MEDIA);
  m->Ranges[1].Physical = 1024;
  assert (SfbImageDiskInit (&d, m) == EFI_VOLUME_CORRUPTED);
  free (Storage);
  free (expected);
  free (m);
  puts ("PASS image Block I/O: fragment boundaries, 4K RMW preservation, limits, stale media, "
        "ownership and I/O failures");
  return 0;
}
