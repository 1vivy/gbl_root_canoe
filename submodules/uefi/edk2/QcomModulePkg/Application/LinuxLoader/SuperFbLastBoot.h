/* Versioned pre-entry diagnostic handoff. Never proof of Android/TEE success. */
#ifndef SUPER_FB_LAST_BOOT_H
#define SUPER_FB_LAST_BOOT_H
#include "Hook/SuperFbProfile.h"
#define SFB_LAST_BOOT_BYTES 256u
#define SFB_LAST_BOOT_HANDOFF 1u
#define SFB_LAST_BOOT_RETURNED 2u
#define SFB_LAST_BOOT_UNMANAGED 3u
/* Byte offsets are the wire ABI. Integers are little endian; reserved bytes zero.
 * 0 CNLB, 4 version, 5 phase, 6 requested, 7 effective, 8 fallback,
 * 9 DeviceInfo (bit0 available, bit1 unlocked, bit2 critical), 10 GPT slot,
 * 11 profile present, 12/13 retries (255 unknown), 16 GM2P[120],
 * 136 tzmap ABL digest[32], 168 BDS version[48], 216 monotonic attempt[8],
 * 252 FNV-1a of bytes [0,252). Checksum detects torn writes, not tampering.
 */
SFB_UINT32 SfbLastBootChecksum(const SFB_UINT8 *Bytes);
SFB_BOOLEAN SfbLastBootValid(const SFB_UINT8 *Bytes, SFB_UINTN Size);
#ifndef SFB_HOST_BUILD
EFI_STATUS SfbLastBootWrite(IN CONST UINT8 *Bytes);
#endif
#endif
