#include "SuperFbProfile.h"

static SFB_UINT16
SfbProfileRead16 (const SFB_UINT8 *Bytes)
{
  return (SFB_UINT16)Bytes[0] | ((SFB_UINT16)Bytes[1] << 8);
}

static SFB_UINT32
SfbProfileRead32 (const SFB_UINT8 *Bytes)
{
  return (SFB_UINT32)Bytes[0] |
         ((SFB_UINT32)Bytes[1] << 8) |
         ((SFB_UINT32)Bytes[2] << 16) |
         ((SFB_UINT32)Bytes[3] << 24);
}

static void
SfbProfileCopy (SFB_UINT8 *Destination, const SFB_UINT8 *Source, SFB_UINTN Length)
{
  SFB_UINTN Index;
  for (Index = 0; Index < Length; ++Index) {
    Destination[Index] = Source[Index];
  }
}

SFB_BOOLEAN
SfbProfileParse (
  const SFB_UINT8 *Bytes,
  SFB_UINTN        Size,
  SFB_MODE2_PROFILE *Profile
  )
{
  if (Bytes == NULL || Profile == NULL || Size != SFB_MODE2_PROFILE_BYTES) {
    return FALSE;
  }

  if (Bytes[0] != 'G' || Bytes[1] != 'M' ||
      Bytes[2] != '2' || Bytes[3] != 'P' ||
      SfbProfileRead16 (Bytes + 4) != SFB_MODE2_PROFILE_VERSION ||
      SfbProfileRead16 (Bytes + 6) != 0 ||
      SfbProfileRead32 (Bytes + 8) != 0 ||
      SfbProfileRead32 (Bytes + 12) != SFB_MODE2_PROFILE_COLOR_GREEN) {
    return FALSE;
  }

  /* Decode scalars explicitly as little-endian; digest fields are octets. */
  SfbProfileCopy ((SFB_UINT8 *)Profile, Bytes, 4);
  Profile->Version = SfbProfileRead16 (Bytes + 4);
  Profile->Reserved = SfbProfileRead16 (Bytes + 6);
  Profile->IsUnlocked = SfbProfileRead32 (Bytes + 8);
  Profile->Color = SfbProfileRead32 (Bytes + 12);
  Profile->SystemVersion = SfbProfileRead32 (Bytes + 16);
  Profile->SystemSpl = SfbProfileRead32 (Bytes + 20);
  SfbProfileCopy (Profile->RotDigest, Bytes + 24, 32);
  SfbProfileCopy (Profile->PubkeyDigest, Bytes + 56, 32);
  SfbProfileCopy (Profile->Vbh, Bytes + 88, 32);
  return TRUE;
}
