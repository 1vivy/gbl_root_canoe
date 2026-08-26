#ifndef __SUPER_FB_LAUNCH_POLICY_H__
#define __SUPER_FB_LAUNCH_POLICY_H__

#include "SuperFbMenu.h"
#include "Hook/SuperFbProfile.h"
#include "Hook/SuperFbTzMap.h"

/* Read the exact sidecar beside Entry->Path. */
EFI_STATUS
SfbLoadMode2Profile (
  IN CONST SFB_BOOT_ENTRY *Entry,
  OUT SFB_MODE2_PROFILE   *Profile
  );

/* Read the exact ABL-derived sidecar beside Entry->Path. */
EFI_STATUS
SfbLoadTzMap (
  IN CONST SFB_BOOT_ENTRY *Entry,
  OUT SFB_TZ_MAP          *Map
  );

/* Resolve a managed launch's effective mode. Profile I/O/validation failures
 * are deliberately reported separately: the launch continues honestly in
 * Mode 0, while callers can display the original failure. */
EFI_STATUS
SfbResolveManagedAblMode (
  IN CONST SFB_BOOT_ENTRY *Entry,
  IN SFB_BOOT_MODE        RequestedMode,
  OUT SFB_BOOT_MODE       *EffectiveMode,
  OUT SFB_MODE2_PROFILE   *Profile,
  OUT EFI_STATUS          *ProfileStatus
  );

/* Security protocol bypass used by both entry and driver-image launches. */
VOID
SfbBypassSecurity (VOID);

VOID
SfbRestoreSecurity (VOID);
/*
 * Set the lock-state policy for the next managed launch. The menu supplies
 * this from canoe.cfg; callers outside a parsed config use as-needed.
 */
VOID
SfbSetLaunchLockPolicy (IN SFB_CONFIG_LOCK_POLICY Policy);

/* Run the real managed-image lifecycle. The caller has already preloaded
 * drivers; this function performs prepare, LoadImage, StartImage, and the
 * required restore/disarm boundaries using gBS directly. */
EFI_STATUS
SfbLaunchImage (
  IN EFI_DEVICE_PATH_PROTOCOL       *DevicePath,
  IN BOOLEAN                         Managed,
  IN SFB_BOOT_MODE                   EffectiveMode,
  IN CONST SFB_MODE2_PROFILE        *Profile,
  IN CONST SFB_TZ_MAP               *TzMap
  );

#endif
