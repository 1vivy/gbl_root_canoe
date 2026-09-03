/* Durable last-managed-launch value shared by policy, logfs and fastboot. */
#ifndef __SUPER_FB_LAST_LAUNCH_H__
#define __SUPER_FB_LAST_LAUNCH_H__

#include "Hook/SuperFbProfile.h"

#define SFB_LAST_LAUNCH_VALUE_BYTES 80u

typedef enum {
  SfbLaunchReasonNone = 0,
  SfbLaunchReasonProfileAbsent,
  SfbLaunchReasonLockstateRefused
} SFB_LAUNCH_REASON;

typedef struct {
  SFB_UINT32 Requested;
  SFB_UINT32 Effective;
  SFB_LAUNCH_REASON Reason;
} SFB_LAST_LAUNCH;

SFB_BOOLEAN
SfbLastLaunchFormat (
  const SFB_LAST_LAUNCH *Launch,
  char                  *Buffer,
  SFB_UINTN              BufferBytes
  );

SFB_BOOLEAN
SfbLastLaunchParse (
  const char      *Value,
  SFB_UINTN        ValueBytes,
  SFB_LAST_LAUNCH *Launch
  );

#endif
