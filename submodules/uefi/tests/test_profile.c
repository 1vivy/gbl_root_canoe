#include "../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbManagedPath.h"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbProfile.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void WriteU16(uint8_t *Data, uint16_t Value)
{
    Data[0] = (uint8_t)Value;
    Data[1] = (uint8_t)(Value >> 8);
}

static void WriteU32(uint8_t *Data, uint32_t Value)
{
    Data[0] = (uint8_t)Value;
    Data[1] = (uint8_t)(Value >> 8);
    Data[2] = (uint8_t)(Value >> 16);
    Data[3] = (uint8_t)(Value >> 24);
}

static void MakeProfile(uint8_t Bytes[SFB_MODE2_PROFILE_BYTES])
{
    size_t Index;

    memset(Bytes, 0, SFB_MODE2_PROFILE_BYTES);
    memcpy(Bytes, "GM2P", 4);
    WriteU16(Bytes + 4, 1);
    WriteU32(Bytes + 16, 0x40007);
    WriteU32(Bytes + 20, 0x9a5);
    for (Index = 0; Index < 32; ++Index) {
        Bytes[24 + Index] = (uint8_t)Index;
        Bytes[56 + Index] = (uint8_t)(0x40 + Index);
        Bytes[88 + Index] = (uint8_t)(0x80 + Index);
    }
}

static void TestExactProfileAbi(void)
{
    uint8_t Bytes[SFB_MODE2_PROFILE_BYTES + 1];
    SFB_MODE2_PROFILE Profile;

    // Given: one exact locked/green 120-byte profile.
    MakeProfile(Bytes);

    // When: the firmware parser decodes it.
    assert(SfbProfileParse(Bytes, SFB_MODE2_PROFILE_BYTES, &Profile));

    // Then: scalar and digest fields retain the exact wire values.
    assert(Profile.IsUnlocked == 0);
    assert(Profile.Color == 0);
    assert(Profile.SystemVersion == 0x40007);
    assert(Profile.SystemSpl == 0x9a5);
    assert(memcmp(Profile.RotDigest, Bytes + 24, 32) == 0);
    assert(memcmp(Profile.PubkeyDigest, Bytes + 56, 32) == 0);
    assert(memcmp(Profile.Vbh, Bytes + 88, 32) == 0);

    // Given/When/Then: short and long payloads are distinguishable and rejected.
    assert(!SfbProfileParse(Bytes, SFB_MODE2_PROFILE_BYTES - 1, &Profile));
    Bytes[SFB_MODE2_PROFILE_BYTES] = 0;
    assert(!SfbProfileParse(Bytes, SFB_MODE2_PROFILE_BYTES + 1, &Profile));

    // Header magic, version, and reserved bits are part of the exact ABI.
    MakeProfile(Bytes);
    Bytes[0] = 'X';
    assert(!SfbProfileParse(Bytes, SFB_MODE2_PROFILE_BYTES, &Profile));
    MakeProfile(Bytes);
    WriteU16(Bytes + 4, 2);
    assert(!SfbProfileParse(Bytes, SFB_MODE2_PROFILE_BYTES, &Profile));
    MakeProfile(Bytes);
    WriteU16(Bytes + 6, 1);
    assert(!SfbProfileParse(Bytes, SFB_MODE2_PROFILE_BYTES, &Profile));

    // Given/When/Then: non-locked and non-green profiles cannot enable Mode 2.
    MakeProfile(Bytes);
    WriteU32(Bytes + 8, 1);
    assert(!SfbProfileParse(Bytes, SFB_MODE2_PROFILE_BYTES, &Profile));
    MakeProfile(Bytes);
    WriteU32(Bytes + 12, 1);
    assert(!SfbProfileParse(Bytes, SFB_MODE2_PROFILE_BYTES, &Profile));
}

#define PATH(Name, ...) static const SFB_PATH_CHAR16 Name[] = {__VA_ARGS__, 0}

static void TestManagedPaths(void)
{
    PATH(Boot, '\\', 'b', 'o', 'o', 't', '.', 'e', 'f', 'i');
    PATH(Backup, '\\', 'b', 'o', 'o', 't', '_', 'b', 'a', 'c', 'k', 'u', 'p', '.', 'e', 'f', 'i');
    PATH(PersistBoot, '\\', 'e', 'f', 'i', 's', 'p', '\\', 'b', 'o', 'o', 't', '.', 'e', 'f', 'i');
    PATH(PersistBackupUpper, '\\', 'E', 'F', 'I', 'S', 'P', '\\', 'B', 'O', 'O', 'T', '_', 'B', 'A', 'C', 'K', 'U', 'P', '.', 'E', 'F', 'I');
    PATH(Relative, 'b', 'o', 'o', 't', '.', 'e', 'f', 'i');
    PATH(Suffix, '\\', 'b', 'o', 'o', 't', '.', 'e', 'f', 'i', '.', 'g', 'm', '2', 'p');
    PATH(Other, '\\', 'e', 'f', 'i', '\\', 'b', 'o', 'o', 't', '\\', 'b', 'o', 'o', 't', 'a', 'a', '6', '4', '.', 'e', 'f', 'i');
    PATH(AliasDot, '\\', '.', '\\', 'b', 'o', 'o', 't', '.', 'e', 'f', 'i');
    PATH(AliasParent, '\\', 'e', 'f', 'i', 's', 'p', '\\', '.', '.', '\\', 'b', 'o', 'o', 't', '.', 'e', 'f', 'i');
    PATH(AliasDouble, '\\', '\\', 'b', 'o', 'o', 't', '.', 'e', 'f', 'i');
    PATH(AliasTrailing, '\\', 'b', 'o', 'o', 't', '.', 'e', 'f', 'i', '\\');

    // Given/When/Then: only the four canonical paths classify as managed.
    assert(SfbIsManagedAblPath(Boot));
    assert(SfbIsManagedAblPath(Backup));
    assert(SfbIsManagedAblPath(PersistBoot));
    assert(SfbIsManagedAblPath(PersistBackupUpper));
    assert(!SfbIsManagedAblPath(Relative));
    assert(!SfbIsManagedAblPath(Suffix));
    assert(!SfbIsManagedAblPath(Other));
    assert(!SfbIsManagedAblPath(AliasDot));
    assert(!SfbIsManagedAblPath(AliasParent));
    assert(!SfbIsManagedAblPath(AliasDouble));
    assert(!SfbIsManagedAblPath(AliasTrailing));
    assert(!SfbIsManagedAblPath(NULL));
}

int main(void)
{
    TestExactProfileAbi();
    TestManagedPaths();
    puts("profile and managed-path tests passed");
    return 0;
}
