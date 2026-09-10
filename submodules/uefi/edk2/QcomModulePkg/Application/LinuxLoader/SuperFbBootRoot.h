/* Shared boot-root observation and fastboot value formatter. */
#ifndef __SUPER_FB_BOOT_ROOT_H__
#define __SUPER_FB_BOOT_ROOT_H__

#include "Hook/SuperFbProfile.h"

#define SFB_BOOT_ROOT_VALUE_BYTES  32u

typedef enum {
  SfbBootRootPopulatedConfig = 0,
  SfbBootRootPopulatedManaged = 1,
  SfbBootRootEmptyRoot = 2,
  SfbBootRootNoRoot = 3,
  SfbBootRootNoVolumes = 4,
  SfbBootRootUnavailable = 5
} SFB_BOOT_ROOT_STATE;

/* Always emits one of the pinned reason literals when Buffer fits. */
SFB_BOOLEAN
SfbBootRootFormat (
  SFB_BOOT_ROOT_STATE  State,
  char                *Buffer,
  SFB_UINTN            BufferBytes
  );

SFB_BOOLEAN SfbBootRootIsEmptyState (SFB_BOOT_ROOT_STATE State);

#ifndef SFB_HOST_BUILD
typedef struct {
  SFB_BOOLEAN          Available;
  SFB_BOOT_ROOT_STATE  State;
} SFB_BOOT_ROOT_OBSERVATION;

VOID SfbRecordBootRootState (IN SFB_BOOT_ROOT_STATE State);
SFB_BOOT_ROOT_OBSERVATION SfbGetBootRootState (VOID);
VOID SfbPublishBootRootTable (VOID);

/*
 * A root is populated only when it contains a launchable managed loader or a
 * valid config naming an existing image. A missing root is first-run; a failed mount is unavailable,
 * not evidence of an empty installation.
 */
SFB_BOOT_ROOT_STATE SfbBootRootObserve (VOID);
#endif

#endif
