#include <assert.h>
#include <stdio.h>
#undef NULL
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbMsdLease.h"
static UINTN Stops, Releases, DiskReleases, Flushes;
static BOOLEAN FailAssign, FailStop, FailRelease, FailDisk, FailFlush;
static EFI_STATUS EFIAPI assign (SFB_USB_MSD_PROTOCOL *m, EFI_BLOCK_IO_PROTOCOL *b, UINT32 lun)
{
  (void)m;
  assert (lun == 0);
  if (b)
    return FailAssign ? EFI_DEVICE_ERROR : EFI_SUCCESS;
  Releases++;
  return FailRelease ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI stop (SFB_USB_MSD_PROTOCOL *m)
{
  (void)m;
  Stops++;
  return FailStop ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI noop (SFB_USB_MSD_PROTOCOL *m)
{
  (void)m;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI flush (EFI_BLOCK_IO_PROTOCOL *b)
{
  (void)b;
  Flushes++;
  return FailFlush ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS release_disk (BOOLEAN done)
{
  assert (done);
  DiskReleases++;
  return FailDisk ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
int main (void)
{
  SFB_USB_MSD_PROTOCOL m = {0};
  EFI_BLOCK_IO_PROTOCOL b = {0};
  m.AssignBlkIoHandle = assign;
  m.StopDevice = stop;
  m.StartDevice = noop;
  m.EventHandler = noop;
  b.FlushBlocks = flush;
  assert (SfbMsdLeaseIdle ());
  FailAssign = TRUE;
  assert (SfbMsdLeaseAssign (&m, &b, release_disk) == EFI_DEVICE_ERROR);
  assert (!SfbMsdLeaseIdle ());
  assert (SfbMsdLeaseAssign (&m, &b, NULL) == EFI_ACCESS_DENIED);
  FailStop = TRUE;
  assert (SfbMsdLeaseFinish () == EFI_DEVICE_ERROR);
  assert (Releases == 0 && DiskReleases == 0);
  FailStop = FALSE;
  FailRelease = TRUE;
  assert (SfbMsdLeaseFinish () == EFI_DEVICE_ERROR);
  assert (Stops == 2 && DiskReleases == 0);
  FailRelease = FALSE;
  FailDisk = TRUE;
  assert (SfbMsdLeaseFinish () == EFI_DEVICE_ERROR);
  assert (Stops == 2 && Releases == 2 && DiskReleases == 1);
  FailDisk = FALSE;
  assert (SfbMsdLeaseFinish () == EFI_SUCCESS);
  assert (SfbMsdLeaseIdle ());
  assert (Stops == 2 && Releases == 2 && DiskReleases == 2);
  FailAssign = FALSE;
  assert (SfbMsdLeaseAssign (&m, &b, NULL) == EFI_SUCCESS);
  FailFlush = TRUE;
  assert (SfbMsdLeaseFinish () == EFI_DEVICE_ERROR);
  assert (!SfbMsdLeaseIdle ());
  FailFlush = FALSE;
  assert (SfbMsdLeaseFinish () == EFI_SUCCESS);
  assert (Stops == 3 && Releases == 3 && Flushes == 2);
  assert (SfbMsdLeaseIdle ());
  assert (SfbMsdLeaseFinish () == EFI_SUCCESS);
  puts ("PASS MSD ownership: partial assignment, stop/release/flush failure retention and "
        "unfinished-only cleanup retry");
  return 0;
}
