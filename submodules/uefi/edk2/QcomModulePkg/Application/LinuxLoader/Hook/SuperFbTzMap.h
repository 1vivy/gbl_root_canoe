/* Pure ABL-derived TrustZone interface map ABI and parser. */
#ifndef __SUPER_FB_TZ_MAP_H__
#define __SUPER_FB_TZ_MAP_H__

#include "SuperFbProfile.h"

#define SFB_TZMAP_BYTES        256u
#define SFB_TZMAP_VERSION      1u
#define SFB_TZMAP_MAX_COMMANDS 16u

#define SFB_TZ_SEMANTIC_UNKNOWN            0u
#define SFB_TZ_SEMANTIC_SET_ROT            1u
#define SFB_TZ_SEMANTIC_SET_VERSION        2u
#define SFB_TZ_SEMANTIC_SET_BOOTSTATE      3u
#define SFB_TZ_SEMANTIC_SET_VBH            4u
#define SFB_TZ_SEMANTIC_READ_DEVICE_STATE  5u
#define SFB_TZ_SEMANTIC_WRITE_DEVICE_STATE 6u
#define SFB_TZ_SEMANTIC_GET_VERSION        7u
#define SFB_TZ_SEMANTIC_MILESTONE          8u
#define SFB_TZ_SEMANTIC_MAX                8u

#define SFB_TZMAP_FLAG_SPSS_CONSUMED   0x00000001u
#define SFB_TZMAP_FLAG_APP_KEYMASTER   0x00000002u
#define SFB_TZMAP_FLAG_APP_KEYMASTER64 0x00000004u
#define SFB_TZMAP_FLAG_APP_OPLUS_SEC   0x00000008u
#define SFB_TZMAP_FLAG_QSEE_CONSUMED   0x00000010u
#define SFB_TZMAP_FLAG_VB_CONSUMED     0x00000020u
#define SFB_TZMAP_FLAG_ALL             0x0000003Fu

#pragma pack(push, 1)
typedef struct {
  SFB_UINT16 Command;      /* 0x00 QSEE command id; 0 marks an unused slot */
  SFB_UINT16 RequestBytes; /* 0x02 exact request length observed; 0 = unknown */
  SFB_UINT8  Semantic;     /* 0x04 SFB_TZ_SEMANTIC_* */
  SFB_UINT8  Occurrences;  /* 0x05 distinct ABL call sites, saturating at 255 */
  SFB_UINT16 Reserved;     /* 0x06 must be 0 */
} SFB_TZ_COMMAND;          /* 8 bytes */

typedef struct {
  SFB_UINT8      Magic[4];      /* 0x00 "GTZM" */
  SFB_UINT16     Version;       /* 0x04 == 1 */
  SFB_UINT16     CommandCount;  /* 0x06 0..16 */
  SFB_UINT32     Flags;         /* 0x08 SFB_TZMAP_FLAG_* */
  SFB_UINT32     Reserved0;     /* 0x0C must be 0 */
  SFB_UINT8      AblDigest[32]; /* 0x10 sha256 of the scanned ABL file */
  SFB_TZ_COMMAND Commands[16];  /* 0x30 */
  SFB_UINT8      Reserved1[80]; /* 0xB0 must be 0 */
} SFB_TZ_MAP;                   /* 256 bytes */
#pragma pack(pop)

#if defined(__cplusplus)
static_assert(sizeof(SFB_TZ_COMMAND) == 8,
              "SFB_TZ_COMMAND must be exactly 8 bytes");
static_assert(sizeof(SFB_TZ_MAP) == SFB_TZMAP_BYTES,
              "SFB_TZ_MAP must be exactly 256 bytes");
static_assert(offsetof(SFB_TZ_MAP, Version) == 4,
              "SFB_TZ_MAP version offset");
static_assert(offsetof(SFB_TZ_MAP, CommandCount) == 6,
              "SFB_TZ_MAP command count offset");
static_assert(offsetof(SFB_TZ_MAP, Flags) == 8,
              "SFB_TZ_MAP flags offset");
static_assert(offsetof(SFB_TZ_MAP, Reserved0) == 12,
              "SFB_TZ_MAP reserved offset");
static_assert(offsetof(SFB_TZ_MAP, AblDigest) == 16,
              "SFB_TZ_MAP digest offset");
static_assert(offsetof(SFB_TZ_MAP, Commands) == 48,
              "SFB_TZ_MAP commands offset");
static_assert(offsetof(SFB_TZ_MAP, Reserved1) == 176,
              "SFB_TZ_MAP trailing reserved offset");
#else
_Static_assert(sizeof(SFB_TZ_COMMAND) == 8,
               "SFB_TZ_COMMAND must be exactly 8 bytes");
_Static_assert(sizeof(SFB_TZ_MAP) == SFB_TZMAP_BYTES,
               "SFB_TZ_MAP must be exactly 256 bytes");
_Static_assert(offsetof(SFB_TZ_MAP, Version) == 4,
               "SFB_TZ_MAP version offset");
_Static_assert(offsetof(SFB_TZ_MAP, CommandCount) == 6,
               "SFB_TZ_MAP command count offset");
_Static_assert(offsetof(SFB_TZ_MAP, Flags) == 8,
               "SFB_TZ_MAP flags offset");
_Static_assert(offsetof(SFB_TZ_MAP, Reserved0) == 12,
               "SFB_TZ_MAP reserved offset");
_Static_assert(offsetof(SFB_TZ_MAP, AblDigest) == 16,
               "SFB_TZ_MAP digest offset");
_Static_assert(offsetof(SFB_TZ_MAP, Commands) == 48,
               "SFB_TZ_MAP commands offset");
_Static_assert(offsetof(SFB_TZ_MAP, Reserved1) == 176,
               "SFB_TZ_MAP trailing reserved offset");
#endif

SFB_BOOLEAN
SfbTzMapParse (
  const SFB_UINT8 *Bytes,
  SFB_UINTN        Size,
  SFB_TZ_MAP      *Map
  );

const SFB_TZ_COMMAND *
SfbTzMapFind (
  const SFB_TZ_MAP *Map,
  SFB_UINT32        Command
  );

/* Resolve the semantic for a command. Firmware-owned: a protocol command always
 * keeps the built-in classification, so a device-writable sidecar cannot
 * reclassify one. See the comment on the definition. */
SFB_UINT8
SfbTzMapSemantic (
  const SFB_TZ_MAP *Map,
  SFB_UINT32        Command
  );

void
SfbTzMapBuiltinDefault (
  SFB_TZ_MAP *Map
  );

#endif
