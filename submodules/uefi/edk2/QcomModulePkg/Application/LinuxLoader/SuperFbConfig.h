/*
 * Writable BOOTCONFIG store on the synthetic FAT volume.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SUPER_FB_CONFIG_H__
#define __SUPER_FB_CONFIG_H__

#include <Uefi.h>

#define SFB_CONFIG_MODE       0
#define SFB_CONFIG_DEFAULT    1
#define SFB_CONFIG_CUSTOM     2
#define SFB_CONFIG_KEYS       3

/* Bind the ordinary Simple File System handle published for efisp.fat. */
VOID
SfbConfigBindVolume (IN EFI_HANDLE Volume);

VOID
SfbConfigUnbindVolume (VOID);

BOOLEAN
SfbConfigVolumeBound (VOID);

EFI_HANDLE
SfbConfigVolume (VOID);


/* Reset the lazy-migration attempt when the bound filesystem changes. */
VOID
SfbStoreResetMigration (VOID);
/*
 * Read one key. FilePresent distinguishes a missing BOOTCONFIG from a file
 * that simply omits this key; ValuePresent is FALSE for the latter.
 */
EFI_STATUS
SfbConfigReadSlot (IN UINTN Slot,
                   OUT CHAR8 *Out,
                   IN UINTN OutBytes,
                   OUT BOOLEAN *FilePresent,
                   OUT BOOLEAN *ValuePresent);

/* Replace one key using a temporary file and a rename. */
EFI_STATUS
SfbConfigWriteSlot (IN UINTN Slot, IN CONST CHAR8 *Value);

/* Write legacy values during the one-way lazy migration. */
EFI_STATUS
SfbConfigMigrate (IN CONST CHAR8 *Mode,
                  IN CONST CHAR8 *Default,
                  IN CONST CHAR8 *Custom,
                  OUT BOOLEAN *Wrote);

#endif
