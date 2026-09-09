/* Retain all borrowed pointers until gadget stop, LUN release and disk flush
 * succeed. Re-entry retries only unfinished stages. SPDX-License-Identifier: BSD-3-Clause */
#include "SuperFbMsdLease.h"
STATIC SFB_USB_MSD_PROTOCOL *mOwner;
STATIC SFB_MSD_RELEASE_DISK mRelease;
STATIC EFI_BLOCK_IO_PROTOCOL *mDisk;
STATIC BOOLEAN mStopped, mUnassigned;
BOOLEAN SfbMsdLeaseIdle (VOID) { return mOwner == NULL; }
EFI_STATUS SfbMsdLeaseFinish (VOID)
{
  EFI_STATUS Status;
  if (mOwner == NULL)
    return EFI_SUCCESS;
  if (!mStopped)
  {
    Status = mOwner->StopDevice (mOwner);
    if (EFI_ERROR (Status) && Status != EFI_NOT_STARTED)
      return Status;
    mStopped = TRUE;
  }
  if (!mUnassigned)
  {
    Status = mOwner->AssignBlkIoHandle (mOwner, NULL, 0);
    if (EFI_ERROR (Status))
      return Status;
    mUnassigned = TRUE;
  }
  if (mRelease != NULL)
  {
    Status = mRelease (TRUE);
    if (EFI_ERROR (Status))
      return Status;
  }
  if (mRelease == NULL)
  {
    Status = mDisk->FlushBlocks (mDisk);
    if (EFI_ERROR (Status))
      return Status;
  }
  mOwner = NULL;
  mRelease = NULL;
  mDisk = NULL;
  return EFI_SUCCESS;
}
EFI_STATUS SfbMsdLeaseAssign (SFB_USB_MSD_PROTOCOL *Msd, EFI_BLOCK_IO_PROTOCOL *Disk,
                              SFB_MSD_RELEASE_DISK Release)
{
  if (mOwner != NULL)
    return EFI_ACCESS_DENIED;
  if (Msd == NULL || Disk == NULL || Msd->AssignBlkIoHandle == NULL || Msd->StopDevice == NULL ||
      Msd->StartDevice == NULL || Msd->EventHandler == NULL || Disk->FlushBlocks == NULL)
    return EFI_INVALID_PARAMETER;
  /* Claim before assigning: even an error may leave a partial LUN binding. */
  mOwner = Msd;
  mDisk = Disk;
  mRelease = Release;
  mStopped = FALSE;
  mUnassigned = FALSE;
  return Msd->AssignBlkIoHandle (Msd, Disk, 0);
}
