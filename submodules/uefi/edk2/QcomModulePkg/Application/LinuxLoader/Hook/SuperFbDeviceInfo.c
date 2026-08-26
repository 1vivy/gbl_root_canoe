#include "SuperFbDeviceInfo.h"

#define SFB_DEVICE_INFO_BYTES            3344u
#define SFB_DEVICE_INFO_MAGIC_BYTES      13u
#define SFB_DEVICE_INFO_UNLOCK_OFFSET    13u
#define SFB_DEVICE_INFO_CRITICAL_OFFSET  14u

#pragma pack(push, 1)
typedef struct {
  SFB_UINT8 Magic[SFB_DEVICE_INFO_MAGIC_BYTES];
  SFB_UINT8 IsUnlocked;
  SFB_UINT8 IsUnlockCritical;
  SFB_UINT8 Reserved[SFB_DEVICE_INFO_BYTES -
                     SFB_DEVICE_INFO_CRITICAL_OFFSET - 1u];
} SFB_DEVICE_INFO_LAYOUT;
#pragma pack(pop)

#if defined(__cplusplus)
static_assert(sizeof(SFB_DEVICE_INFO_LAYOUT) == SFB_DEVICE_INFO_BYTES,
              "DeviceInfo ABI size");
static_assert(offsetof(SFB_DEVICE_INFO_LAYOUT, IsUnlocked) ==
              SFB_DEVICE_INFO_UNLOCK_OFFSET,
              "DeviceInfo unlock offset");
static_assert(offsetof(SFB_DEVICE_INFO_LAYOUT, IsUnlockCritical) ==
              SFB_DEVICE_INFO_CRITICAL_OFFSET,
              "DeviceInfo critical-unlock offset");
#else
_Static_assert(sizeof(SFB_DEVICE_INFO_LAYOUT) == SFB_DEVICE_INFO_BYTES,
               "DeviceInfo ABI size");
_Static_assert(offsetof(SFB_DEVICE_INFO_LAYOUT, IsUnlocked) ==
               SFB_DEVICE_INFO_UNLOCK_OFFSET,
               "DeviceInfo unlock offset");
_Static_assert(offsetof(SFB_DEVICE_INFO_LAYOUT, IsUnlockCritical) ==
               SFB_DEVICE_INFO_CRITICAL_OFFSET,
               "DeviceInfo critical-unlock offset");
#endif

static const SFB_UINT8 mDeviceInfoMagic[SFB_DEVICE_INFO_MAGIC_BYTES] = {
  'A', 'N', 'D', 'R', 'O', 'I', 'D', '-', 'B', 'O', 'O', 'T', '!'
};

SFB_BOOLEAN
SfbDeviceInfoValid (const SFB_UINT8 *Bytes, SFB_UINTN Size)
{
  SFB_UINTN Index;

  if (Bytes == NULL || Size < SFB_DEVICE_INFO_BYTES) {
    return FALSE;
  }
  for (Index = 0; Index < SFB_DEVICE_INFO_MAGIC_BYTES; ++Index) {
    if (Bytes[Index] != mDeviceInfoMagic[Index]) {
      return FALSE;
    }
  }
  return TRUE;
}

SFB_BOOLEAN
SfbDeviceInfoReadLock (const SFB_UINT8 *Bytes, SFB_UINTN Size,
                       SFB_BOOLEAN *Unlocked, SFB_BOOLEAN *Critical)
{
  if (Unlocked == NULL || Critical == NULL ||
      !SfbDeviceInfoValid (Bytes, Size)) {
    return FALSE;
  }
  *Unlocked = (SFB_BOOLEAN)(Bytes[SFB_DEVICE_INFO_UNLOCK_OFFSET] != 0);
  *Critical = (SFB_BOOLEAN)(Bytes[SFB_DEVICE_INFO_CRITICAL_OFFSET] != 0);
  return TRUE;
}

SFB_BOOLEAN
SfbDeviceInfoSetLock (SFB_UINT8 *Bytes, SFB_UINTN Size,
                      SFB_BOOLEAN WantUnlocked, SFB_BOOLEAN WantCritical,
                      SFB_LOCK_ACTION *Action)
{
  SFB_BOOLEAN CurrentUnlocked;
  SFB_BOOLEAN CurrentCritical;
  SFB_BOOLEAN DesiredUnlocked;
  SFB_BOOLEAN DesiredCritical;

  if (Action == NULL) {
    return FALSE;
  }
  *Action = SfbLockActionNone;
  if (!SfbDeviceInfoReadLock (Bytes, Size, &CurrentUnlocked,
                              &CurrentCritical)) {
    return FALSE;
  }

  DesiredCritical = (SFB_BOOLEAN)(WantCritical != FALSE);
  DesiredUnlocked = (SFB_BOOLEAN)(WantUnlocked != FALSE ||
                                  DesiredCritical != FALSE);
  if ((CurrentUnlocked != FALSE) == (DesiredUnlocked != FALSE) &&
      (CurrentCritical != FALSE) == (DesiredCritical != FALSE) &&
      Bytes[SFB_DEVICE_INFO_UNLOCK_OFFSET] == (SFB_UINT8)DesiredUnlocked &&
      Bytes[SFB_DEVICE_INFO_CRITICAL_OFFSET] == (SFB_UINT8)DesiredCritical) {
    return TRUE;
  }

  Bytes[SFB_DEVICE_INFO_UNLOCK_OFFSET] = (SFB_UINT8)DesiredUnlocked;
  Bytes[SFB_DEVICE_INFO_CRITICAL_OFFSET] = (SFB_UINT8)DesiredCritical;
  *Action = SfbLockActionWrite;
  return TRUE;
}
