/* Pure GM2P profile ABI and parser. */
#ifndef __SUPER_FB_PROFILE_H__
#define __SUPER_FB_PROFILE_H__

#ifdef SFB_HOST_BUILD
#include <stdint.h>
#include <stddef.h>
typedef uint8_t  SFB_UINT8;
typedef uint16_t SFB_UINT16;
typedef uint32_t SFB_UINT32;
typedef size_t   SFB_UINTN;
typedef int      SFB_BOOLEAN;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#else
#include <Uefi.h>
#include <stddef.h>
typedef UINT8  SFB_UINT8;
typedef UINT16 SFB_UINT16;
typedef UINT32 SFB_UINT32;
typedef UINTN  SFB_UINTN;
typedef BOOLEAN SFB_BOOLEAN;
#endif

#define SFB_MODE2_PROFILE_BYTES 120u
#define SFB_MODE2_PROFILE_VERSION 1u
#define SFB_MODE2_PROFILE_COLOR_GREEN 0u

#pragma pack(push, 1)
typedef struct {
  SFB_UINT8  Magic[4];
  SFB_UINT16 Version;
  SFB_UINT16 Reserved;
  SFB_UINT32 IsUnlocked;
  SFB_UINT32 Color;
  SFB_UINT32 SystemVersion;
  SFB_UINT32 SystemSpl;
  SFB_UINT8  RotDigest[32];
  SFB_UINT8  PubkeyDigest[32];
  SFB_UINT8  Vbh[32];
} SFB_MODE2_PROFILE;
#pragma pack(pop)

#if defined(__cplusplus)
static_assert(sizeof(SFB_MODE2_PROFILE) == SFB_MODE2_PROFILE_BYTES,
              "SFB_MODE2_PROFILE must be exactly 120 bytes");
static_assert(offsetof(SFB_MODE2_PROFILE, Version) == 4,
              "SFB_MODE2_PROFILE version offset");
static_assert(offsetof(SFB_MODE2_PROFILE, IsUnlocked) == 8,
              "SFB_MODE2_PROFILE unlocked offset");
static_assert(offsetof(SFB_MODE2_PROFILE, Color) == 12,
              "SFB_MODE2_PROFILE color offset");
static_assert(offsetof(SFB_MODE2_PROFILE, SystemVersion) == 16,
              "SFB_MODE2_PROFILE version field offset");
static_assert(offsetof(SFB_MODE2_PROFILE, SystemSpl) == 20,
              "SFB_MODE2_PROFILE spl offset");
static_assert(offsetof(SFB_MODE2_PROFILE, RotDigest) == 24,
              "SFB_MODE2_PROFILE rot offset");
static_assert(offsetof(SFB_MODE2_PROFILE, PubkeyDigest) == 56,
              "SFB_MODE2_PROFILE key offset");
static_assert(offsetof(SFB_MODE2_PROFILE, Vbh) == 88,
              "SFB_MODE2_PROFILE vbh offset");
#else
_Static_assert(sizeof(SFB_MODE2_PROFILE) == SFB_MODE2_PROFILE_BYTES,
               "SFB_MODE2_PROFILE must be exactly 120 bytes");
_Static_assert(offsetof(SFB_MODE2_PROFILE, Version) == 4,
               "SFB_MODE2_PROFILE version offset");
_Static_assert(offsetof(SFB_MODE2_PROFILE, IsUnlocked) == 8,
               "SFB_MODE2_PROFILE unlocked offset");
_Static_assert(offsetof(SFB_MODE2_PROFILE, Color) == 12,
               "SFB_MODE2_PROFILE color offset");
_Static_assert(offsetof(SFB_MODE2_PROFILE, SystemVersion) == 16,
               "SFB_MODE2_PROFILE version field offset");
_Static_assert(offsetof(SFB_MODE2_PROFILE, SystemSpl) == 20,
               "SFB_MODE2_PROFILE spl offset");
_Static_assert(offsetof(SFB_MODE2_PROFILE, RotDigest) == 24,
               "SFB_MODE2_PROFILE rot offset");
_Static_assert(offsetof(SFB_MODE2_PROFILE, PubkeyDigest) == 56,
               "SFB_MODE2_PROFILE key offset");
_Static_assert(offsetof(SFB_MODE2_PROFILE, Vbh) == 88,
               "SFB_MODE2_PROFILE vbh offset");
#endif


/* Exact ABI validation. A valid launch profile is locked and green. */
SFB_BOOLEAN
SfbProfileParse (
  const SFB_UINT8 *Bytes,
  SFB_UINTN        Size,
  SFB_MODE2_PROFILE *Profile
  );

#endif
