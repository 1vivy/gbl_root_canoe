/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef SUPER_FB_CONTAINER_H
#define SUPER_FB_CONTAINER_H
#include <Uefi.h>
EFI_STATUS SfbContainerMount (VOID);
EFI_STATUS SfbContainerUnmount (VOID);
BOOLEAN SfbIsContainerVolume (EFI_HANDLE Handle);
#endif
