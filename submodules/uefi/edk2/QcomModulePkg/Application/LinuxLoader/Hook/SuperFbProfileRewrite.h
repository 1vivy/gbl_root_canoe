/* Host-testable KeyMint/SPSS payload transforms. */
#ifndef __SUPER_FB_PROFILE_REWRITE_H__
#define __SUPER_FB_PROFILE_REWRITE_H__

#include "SuperFbProfile.h"

#define SFB_KM_SET_ROT        0x201u
#define SFB_KM_SET_VERSION    0x207u
#define SFB_KM_SET_BOOTSTATE  0x208u
#define SFB_KM_SET_VBH        0x211u
#define SFB_KM_SET_ROT_BYTES  44u
#define SFB_KM_SET_VERSION_BYTES 12u
#define SFB_KM_SET_BOOTSTATE_BYTES 64u
#define SFB_KM_SET_VBH_BYTES  36u
#define SFB_SPSS_INFO_BYTES   144u

SFB_BOOLEAN
SfbRewriteKeymaster (
  SFB_UINT32        Command,
  SFB_UINT8        *Buffer,
  SFB_UINT32        BufferBytes,
  const SFB_MODE2_PROFILE *Profile
  );

SFB_BOOLEAN
SfbRewriteSpss (
  SFB_UINT8        *Buffer,
  SFB_UINT32        BufferBytes,
  const SFB_MODE2_PROFILE *Profile
  );

#endif
