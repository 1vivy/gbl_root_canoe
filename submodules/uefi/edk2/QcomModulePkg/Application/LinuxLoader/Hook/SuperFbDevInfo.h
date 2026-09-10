/* Shared observed DeviceInfo state and fastboot value formatter. */
#ifndef __SUPER_FB_DEV_INFO_H__
#define __SUPER_FB_DEV_INFO_H__

#include "SuperFbProfile.h"
#include "../SuperFbSlotRetry.h"

#define SFB_DEVINFO_VALUE_BYTES 61u
#define SFB_SLOT_RETRIES_VALUE_BYTES 32u

typedef struct {
  SFB_BOOLEAN Available;
  SFB_BOOLEAN Unlocked;
  SFB_BOOLEAN Critical;
} SFB_OBSERVED_DEVINFO;

/* Each value fits the fastboot 60-byte payload. Unknown observations remain
 * unknown; slot retry metadata has a separate diagnostic variable. */
SFB_BOOLEAN
SfbFormatObservedDevInfo (
  const SFB_OBSERVED_DEVINFO *Observed,
  char                       *Buffer,
  SFB_UINTN                   BufferBytes
  );

SFB_BOOLEAN
SfbFormatSlotRetries (
  const SFB_SLOT_RETRIES *Retries, char *Buffer, SFB_UINTN BufferBytes);

#ifndef SFB_HOST_BUILD
VOID SfbRecordObservedDevInfo (IN BOOLEAN Unlocked, IN BOOLEAN Critical);
VOID SfbInvalidateObservedDevInfo (VOID);
SFB_OBSERVED_DEVINFO SfbGetObservedDevInfo (VOID);
#endif

#endif
