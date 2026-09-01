/* Read-only A/B retry state shared by GPT discovery and getvar formatting. */
#ifndef __SUPER_FB_SLOT_RETRY_H__
#define __SUPER_FB_SLOT_RETRY_H__

#include "Hook/SuperFbProfile.h"

typedef struct {
  SFB_BOOLEAN Available;
  SFB_UINT8   RetryCount;
  SFB_BOOLEAN Successful;
  SFB_BOOLEAN Unbootable;
} SFB_SLOT_RETRY_STATE;

typedef struct {
  SFB_SLOT_RETRY_STATE A;
  SFB_SLOT_RETRY_STATE B;
} SFB_SLOT_RETRIES;

#endif /* __SUPER_FB_SLOT_RETRY_H__ */
