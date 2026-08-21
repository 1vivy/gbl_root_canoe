#include "SuperFbProfileRewrite.h"

static void
SfbWrite32 (SFB_UINT8 *Bytes, SFB_UINT32 Value)
{
  Bytes[0] = (SFB_UINT8)Value;
  Bytes[1] = (SFB_UINT8)(Value >> 8);
  Bytes[2] = (SFB_UINT8)(Value >> 16);
  Bytes[3] = (SFB_UINT8)(Value >> 24);
}

static void
SfbCopy (SFB_UINT8 *Destination, const SFB_UINT8 *Source, SFB_UINTN Length)
{
  SFB_UINTN Index;
  for (Index = 0; Index < Length; ++Index) {
    Destination[Index] = Source[Index];
  }
}

static void
SfbRewriteRot (SFB_UINT8 *Buffer, const SFB_MODE2_PROFILE *Profile)
{
  SfbCopy (Buffer + 12, Profile->RotDigest, 32);
}

static void
SfbRewriteVersion (SFB_UINT8 *Buffer, const SFB_MODE2_PROFILE *Profile)
{
  SfbWrite32 (Buffer + 4, Profile->SystemVersion);
  SfbWrite32 (Buffer + 8, Profile->SystemSpl);
}

static void
SfbRewriteBootState (SFB_UINT8 *Buffer, const SFB_MODE2_PROFILE *Profile)
{
  SfbWrite32 (Buffer + 16, Profile->IsUnlocked);
  SfbCopy (Buffer + 20, Profile->PubkeyDigest, 32);
  SfbWrite32 (Buffer + 52, Profile->Color);
  SfbWrite32 (Buffer + 56, Profile->SystemVersion);
  SfbWrite32 (Buffer + 60, Profile->SystemSpl);
}

static void
SfbRewriteVbh (SFB_UINT8 *Buffer, const SFB_MODE2_PROFILE *Profile)
{
  SfbCopy (Buffer + 4, Profile->Vbh, 32);
}

SFB_BOOLEAN
SfbRewriteKeymaster (
  SFB_UINT32 Command,
  SFB_UINT8 *Buffer,
  SFB_UINT32 BufferBytes,
  const SFB_MODE2_PROFILE *Profile
  )
{
  if (Buffer == NULL || Profile == NULL) {
    return FALSE;
  }

  switch (Command) {
    case SFB_KM_SET_ROT:
      if (BufferBytes != SFB_KM_SET_ROT_BYTES) return FALSE;
      SfbRewriteRot (Buffer, Profile);
      return TRUE;
    case SFB_KM_SET_VERSION:
      if (BufferBytes != SFB_KM_SET_VERSION_BYTES) return FALSE;
      SfbRewriteVersion (Buffer, Profile);
      return TRUE;
    case SFB_KM_SET_BOOTSTATE:
      if (BufferBytes != SFB_KM_SET_BOOTSTATE_BYTES) return FALSE;
      SfbRewriteBootState (Buffer, Profile);
      return TRUE;
    case SFB_KM_SET_VBH:
      if (BufferBytes != SFB_KM_SET_VBH_BYTES) return FALSE;
      SfbRewriteVbh (Buffer, Profile);
      return TRUE;
    default:
      return FALSE;
  }
}

SFB_BOOLEAN
SfbRewriteSpss (
  SFB_UINT8 *Buffer,
  SFB_UINT32 BufferBytes,
  const SFB_MODE2_PROFILE *Profile
  )
{
  if (Buffer == NULL || Profile == NULL || BufferBytes < SFB_SPSS_INFO_BYTES) {
    return FALSE;
  }

  SfbRewriteRot (Buffer, Profile);
  SfbRewriteBootState (Buffer + 44, Profile);
  SfbRewriteVbh (Buffer + 108, Profile);
  return TRUE;
}
