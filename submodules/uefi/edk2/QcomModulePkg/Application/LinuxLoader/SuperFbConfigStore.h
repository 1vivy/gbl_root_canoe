/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef SUPER_FB_CONFIG_STORE_H
#define SUPER_FB_CONFIG_STORE_H
#include <Uefi.h>
#include <Protocol/SimpleFileSystem.h>
#include "SuperFbConfig.h"

#define SFB_CONFIG_CURRENT_PATH L"\\canoe.cfg"
#define SFB_CONFIG_PREVIOUS_PATH L"\\canoe.cfg.prev"

/* Buffer must hold SFB_CONFIG_MAX_BYTES + 1 bytes. Missing/malformed current
 * content may use the validated previous generation; I/O errors never do. */
EFI_STATUS SfbReadStoredConfig (EFI_FILE_PROTOCOL *Root, CHAR8 *Buffer,
                                UINTN *Size, SFB_CONFIG *Config,
                                BOOLEAN *Previous);
/* Checked staged writes, with previous generation committed before current
 * replacement. Never called as a consequence of an ordinary boot selection. */
EFI_STATUS SfbStoreConfigDefault (EFI_FILE_PROTOCOL *Root,
                                  CONST CHAR8 *Target, UINT8 Mode);
#endif
