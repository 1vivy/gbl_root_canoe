#include "../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbDeviceInfo.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DEVICE_INFO_BYTES 3344u
#define UNLOCK_OFFSET 13u
#define CRITICAL_OFFSET 14u

static void
MakeDeviceInfo (uint8_t Bytes[DEVICE_INFO_BYTES], uint8_t Unlocked,
                uint8_t Critical)
{
    memset(Bytes, 0, DEVICE_INFO_BYTES);
    memcpy(Bytes, "ANDROID-BOOT!", 13);
    Bytes[UNLOCK_OFFSET] = Unlocked;
    Bytes[CRITICAL_OFFSET] = Critical;
}

static void
TestValidation (void)
{
    uint8_t Bytes[DEVICE_INFO_BYTES];

    MakeDeviceInfo(Bytes, 0, 0);
    assert(SfbDeviceInfoValid(Bytes, sizeof(Bytes)));
    Bytes[0] = 'X';
    assert(!SfbDeviceInfoValid(Bytes, sizeof(Bytes)));
    MakeDeviceInfo(Bytes, 0, 0);
    assert(!SfbDeviceInfoValid(Bytes, sizeof(Bytes) - 1));
}

static void
TestCriticalUnlockImpliesUnlock (void)
{
    uint8_t Bytes[DEVICE_INFO_BYTES];
    SFB_BOOLEAN Unlocked;
    SFB_BOOLEAN Critical;
    SFB_LOCK_ACTION Action;

    MakeDeviceInfo(Bytes, 0, 0);
    assert(SfbDeviceInfoSetLock(Bytes, sizeof(Bytes), FALSE, TRUE, &Action));
    assert(Action == SfbLockActionWrite);
    assert(SfbDeviceInfoReadLock(Bytes, sizeof(Bytes), &Unlocked, &Critical));
    assert(Unlocked == TRUE);
    assert(Critical == TRUE);
}

static void
TestClearingUnlockClearsCritical (void)
{
    uint8_t Bytes[DEVICE_INFO_BYTES];
    SFB_BOOLEAN Unlocked;
    SFB_BOOLEAN Critical;
    SFB_LOCK_ACTION Action;

    MakeDeviceInfo(Bytes, 1, 1);
    assert(SfbDeviceInfoSetLock(Bytes, sizeof(Bytes), FALSE, FALSE, &Action));
    assert(Action == SfbLockActionWrite);
    assert(SfbDeviceInfoReadLock(Bytes, sizeof(Bytes), &Unlocked, &Critical));
    assert(Unlocked == FALSE);
    assert(Critical == FALSE);
}

static void
TestUnchangedStateNeedsNoWrite (void)
{
    uint8_t Bytes[DEVICE_INFO_BYTES];
    uint8_t Before[DEVICE_INFO_BYTES];
    SFB_LOCK_ACTION Action;

    MakeDeviceInfo(Bytes, 1, 1);
    memcpy(Before, Bytes, sizeof(Bytes));
    assert(SfbDeviceInfoSetLock(Bytes, sizeof(Bytes), TRUE, TRUE, &Action));
    assert(Action == SfbLockActionNone);
    assert(memcmp(Bytes, Before, sizeof(Bytes)) == 0);
}

int
main (void)
{
    TestValidation();
    TestCriticalUnlockImpliesUnlock();
    TestClearingUnlockClearsCritical();
    TestUnchangedStateNeedsNoWrite();
    puts("deviceinfo lock tests passed");
    return 0;
}
