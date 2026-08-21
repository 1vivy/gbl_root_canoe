/* Pure managed-ABL path classifier. */
#ifndef __SUPER_FB_MANAGED_PATH_H__
#define __SUPER_FB_MANAGED_PATH_H__

#ifdef SFB_HOST_BUILD
#include <stdint.h>
#include <stddef.h>
typedef uint16_t SFB_PATH_CHAR16;
typedef int SFB_PATH_BOOLEAN;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#else
#include <Uefi.h>
typedef CHAR16 SFB_PATH_CHAR16;
typedef BOOLEAN SFB_PATH_BOOLEAN;
#endif
SFB_PATH_BOOLEAN
SfbIsManagedAblPath (const SFB_PATH_CHAR16 *Path);

#endif
