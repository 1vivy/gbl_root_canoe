/*
 * KeyMint request wire layouts shared by the SPSS and QSEECOM hooks.
 * These are firmware ABI records; do not add natural-alignment padding.
 */
#ifndef __KEYMASTER_CLIENT_H__
#define __KEYMASTER_CLIENT_H__

#include <Uefi.h>
#include <stddef.h>

#pragma pack(push, 1)
typedef struct {
  UINT32 CmdId;
  UINT32 RotOffset;
  UINT32 RotSize;
  UINT8  RotDigest[32];
} KMSetRotReq;

typedef struct {
  UINT32 IsUnlocked;
  UINT8  PublicKey[32];
  UINT32 Color;
  UINT32 SystemVersion;
  UINT32 SystemSecurityLevel;
} KMSetBootState;

typedef struct {
  UINT32 CmdId;
  UINT32 Version;
  UINT32 Offset;
  UINT32 Size;
  KMSetBootState BootState;
} KMSetBootStateReq;

typedef struct {
  UINT32 CmdId;
  UINT8  Vbh[32];
} KMSetVbhReq;
#pragma pack(pop)

_Static_assert (sizeof (KMSetRotReq) == 44, "KMSetRotReq ABI size");
_Static_assert (offsetof (KMSetRotReq, CmdId) == 0, "KMSetRotReq command offset");
_Static_assert (offsetof (KMSetRotReq, RotOffset) == 4, "KMSetRotReq offset field");
_Static_assert (offsetof (KMSetRotReq, RotSize) == 8, "KMSetRotReq size field");
_Static_assert (offsetof (KMSetRotReq, RotDigest) == 12, "KMSetRotReq digest offset");
_Static_assert (sizeof (KMSetBootState) == 48, "KMSetBootState ABI size");
_Static_assert (offsetof (KMSetBootState, IsUnlocked) == 0, "KMSetBootState unlocked offset");
_Static_assert (offsetof (KMSetBootState, PublicKey) == 4, "KMSetBootState key offset");
_Static_assert (offsetof (KMSetBootState, Color) == 36, "KMSetBootState color offset");
_Static_assert (offsetof (KMSetBootState, SystemVersion) == 40, "KMSetBootState version offset");
_Static_assert (offsetof (KMSetBootState, SystemSecurityLevel) == 44, "KMSetBootState spl offset");
_Static_assert (sizeof (KMSetBootStateReq) == 64, "KMSetBootStateReq ABI size");
_Static_assert (offsetof (KMSetBootStateReq, CmdId) == 0, "KMSetBootStateReq command offset");
_Static_assert (offsetof (KMSetBootStateReq, Version) == 4, "KMSetBootStateReq version offset");
_Static_assert (offsetof (KMSetBootStateReq, Offset) == 8, "KMSetBootStateReq offset field");
_Static_assert (offsetof (KMSetBootStateReq, Size) == 12, "KMSetBootStateReq size field");
_Static_assert (offsetof (KMSetBootStateReq, BootState) == 16, "KMSetBootStateReq state offset");
_Static_assert (sizeof (KMSetVbhReq) == 36, "KMSetVbhReq ABI size");
_Static_assert (offsetof (KMSetVbhReq, CmdId) == 0, "KMSetVbhReq command offset");
_Static_assert (offsetof (KMSetVbhReq, Vbh) == 4, "KMSetVbhReq digest offset");

#endif
