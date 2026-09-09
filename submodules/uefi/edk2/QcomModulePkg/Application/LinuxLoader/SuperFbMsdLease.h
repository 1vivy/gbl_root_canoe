/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef SUPER_FB_MSD_LEASE_H
#define SUPER_FB_MSD_LEASE_H
#include "SuperFbMassStorage.h"
typedef EFI_STATUS (*SFB_MSD_RELEASE_DISK) (BOOLEAN Released);
EFI_STATUS SfbMsdLeaseAssign (SFB_USB_MSD_PROTOCOL *Msd, EFI_BLOCK_IO_PROTOCOL *Disk,
                              SFB_MSD_RELEASE_DISK Release);
EFI_STATUS SfbMsdLeaseFinish (VOID);
BOOLEAN SfbMsdLeaseIdle (VOID);
#endif
