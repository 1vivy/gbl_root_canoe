#include "SuperFbLastBoot.h"
SFB_UINT32 SfbLastBootChecksum(const SFB_UINT8 *Bytes)
{
  SFB_UINT32 Hash = 2166136261u;
  SFB_UINTN Index;
  for (Index = 0; Index < 252u; ++Index) Hash = (Hash ^ Bytes[Index]) * 16777619u;
  return Hash;
}
SFB_BOOLEAN SfbLastBootValid(const SFB_UINT8 *B, SFB_UINTN Size)
{
  SFB_UINT32 Stored;
  SFB_UINTN Index;
  if (B == NULL || Size != SFB_LAST_BOOT_BYTES || B[0] != 'C' || B[1] != 'N' ||
      B[2] != 'L' || B[3] != 'B' || B[4] != 1 || B[5] < 1 || B[5] > 3 ||
      B[6] > 2 || B[7] > 2 || B[8] > 2 || B[9] > 7 || B[10] > 2 || B[11] > 1 ||
      (B[12] > 7 && B[12] != 255) || (B[13] > 7 && B[13] != 255) ||
      B[14] != 0 || B[15] != 0 || B[215] != 0) return FALSE;
  for (Index = 224; Index < 252; ++Index) if (B[Index] != 0) return FALSE;
  Stored = (SFB_UINT32)B[252] | ((SFB_UINT32)B[253] << 8) |
           ((SFB_UINT32)B[254] << 16) | ((SFB_UINT32)B[255] << 24);
  return Stored == SfbLastBootChecksum(B);
}
