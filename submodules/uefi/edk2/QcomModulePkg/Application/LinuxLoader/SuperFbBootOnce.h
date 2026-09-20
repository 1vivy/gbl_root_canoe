/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef SUPER_FB_BOOT_ONCE_H
#define SUPER_FB_BOOT_ONCE_H

#include <Uefi.h>

#include "SuperFbMenu.h"

typedef enum {
  SfbBootOnceNone = 0,
  SfbBootOnceMenu,
  SfbBootOnceFastboot
} SFB_BOOT_ONCE_RESULT;

/* Resolve and arm a currently available entry without resetting the device. */
EFI_STATUS
SfbBootOnceArm (IN CONST CHAR8 *Selector);
/* Arm a menu row already proven eligible without rebuilding the active menu. */
EFI_STATUS
SfbBootOnceArmEntry (IN CONST SFB_BOOT_ENTRY *Entry);


/* Clear before resolving or launching; failures fall through to normal policy. */
SFB_BOOT_ONCE_RESULT
SfbBootOnceConsume (IN SFB_BOOT_MODE Mode);

/* Selector represented by a menu row, or NULL when that row cannot be armed. */
CONST CHAR8 *
SfbBootOnceEntrySelector (IN CONST SFB_BOOT_ENTRY *Entry);

/* One-shot handoff to SfbBuildMenu for its existing inert notice surface. */
BOOLEAN
SfbBootOnceTakeRejectedNotice (VOID);

#endif
