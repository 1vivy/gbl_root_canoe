/*
 * Which slot the GPT marks active.
 *
 * This exists to check a claim, never to make one. The menu's slot roles are
 * presentation-only and are written by whoever authored canoe.cfg; when an OTA
 * flips the active slot and the device-side watcher has not run yet - because
 * Android has not booted - the config still labels the old slot active. Reading
 * the GPT is the only way anything in the BDS can notice.
 *
 * Deliberately not built on the vendor slot helpers. GetCurrentSlotSuffix,
 * FindPtnActiveSlot, SetActiveSlot, FindBootableSlot, IsSuffixEmpty,
 * ClearUnbootable, IsSlotsUbootable, PartitionHasMultiSlot and
 * SetMultiSlotBootVal are declarations with no definition anywhere in this
 * tree; calling one links today only because nothing does.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SUPER_FB_SLOTS_H__
#define __SUPER_FB_SLOTS_H__

#include <Uefi.h>

typedef enum {
  /* Not an A/B device, or the GPT marks neither slot, or it marks both. */
  SfbSlotUnknown = 0,
  SfbSlotA,
  SfbSlotB
} SFB_SLOT;

/*
 * Which slot the GPT marks active, read from the partition table the loader
 * already enumerated. Never fails: an unrecognised layout answers
 * SfbSlotUnknown, which every caller must treat as "nothing to check against".
 *
 * The answer is computed once and cached, so the marker it logs appears once
 * per boot however often the menu is rebuilt.
 */
SFB_SLOT
SfbActiveSlot (VOID);

#endif /* __SUPER_FB_SLOTS_H__ */
