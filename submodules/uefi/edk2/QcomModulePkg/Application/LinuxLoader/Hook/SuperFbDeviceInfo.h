/* Pure DeviceInfo lock-pair helper. */
#ifndef __SUPER_FB_DEVICE_INFO_H__
#define __SUPER_FB_DEVICE_INFO_H__

#include "SuperFbProfile.h"

typedef enum {
  SfbLockActionNone = 0,  /* observed state already satisfies the request */
  SfbLockActionWrite       /* caller must write Out back */
} SFB_LOCK_ACTION;

/* FALSE when Bytes is not a valid DeviceInfo blob. */
SFB_BOOLEAN SfbDeviceInfoValid (const SFB_UINT8 *Bytes, SFB_UINTN Size);

/* Read the pair. Requires SfbDeviceInfoValid. */
SFB_BOOLEAN SfbDeviceInfoReadLock (const SFB_UINT8 *Bytes, SFB_UINTN Size,
                                   SFB_BOOLEAN *Unlocked,
                                   SFB_BOOLEAN *Critical);

/*
 * Apply WantUnlocked/WantCritical to a copy of Bytes, enforcing the invariant
 * that critical unlock implies unlock and clearing unlock clears critical.
 * Reports whether a write is actually needed.
 */
SFB_BOOLEAN SfbDeviceInfoSetLock (SFB_UINT8 *Bytes, SFB_UINTN Size,
                                  SFB_BOOLEAN WantUnlocked,
                                  SFB_BOOLEAN WantCritical,
                                  SFB_LOCK_ACTION *Action);

#endif
