#include "../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbTzMap.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void
WriteU16 (uint8_t *Data, uint16_t Value)
{
    Data[0] = (uint8_t)Value;
    Data[1] = (uint8_t)(Value >> 8);
}

static void
WriteU32 (uint8_t *Data, uint32_t Value)
{
    Data[0] = (uint8_t)Value;
    Data[1] = (uint8_t)(Value >> 8);
    Data[2] = (uint8_t)(Value >> 16);
    Data[3] = (uint8_t)(Value >> 24);
}


static void
SerializeMap (uint8_t Bytes[SFB_TZMAP_BYTES], const SFB_TZ_MAP *Map)
{
    size_t Index;
    size_t Offset;

    memset(Bytes, 0, SFB_TZMAP_BYTES);
    memcpy(Bytes, Map->Magic, sizeof(Map->Magic));
    WriteU16(Bytes + 4, Map->Version);
    WriteU16(Bytes + 6, Map->CommandCount);
    WriteU32(Bytes + 8, Map->Flags);
    WriteU32(Bytes + 12, Map->Reserved0);
    memcpy(Bytes + 16, Map->AblDigest, sizeof(Map->AblDigest));
    for (Index = 0; Index < SFB_TZMAP_MAX_COMMANDS; ++Index) {
        Offset = 48 + Index * sizeof(SFB_TZ_COMMAND);
        WriteU16(Bytes + Offset, Map->Commands[Index].Command);
        WriteU16(Bytes + Offset + 2, Map->Commands[Index].RequestBytes);
        Bytes[Offset + 4] = Map->Commands[Index].Semantic;
        Bytes[Offset + 5] = Map->Commands[Index].Occurrences;
        WriteU16(Bytes + Offset + 6, Map->Commands[Index].Reserved);
    }
    memcpy(Bytes + 176, Map->Reserved1, sizeof(Map->Reserved1));
}

static void
MakeValid (uint8_t Bytes[SFB_TZMAP_BYTES + 1])
{
    size_t Index;

    memset(Bytes, 0, SFB_TZMAP_BYTES + 1);
    memcpy(Bytes, "GTZM", 4);
    WriteU16(Bytes + 4, SFB_TZMAP_VERSION);
    WriteU16(Bytes + 6, 2);
    WriteU32(Bytes + 8, SFB_TZMAP_FLAG_SPSS_CONSUMED |
                       SFB_TZMAP_FLAG_APP_KEYMASTER);
    for (Index = 0; Index < sizeof(((SFB_TZ_MAP *)0)->AblDigest); ++Index) {
        Bytes[16 + Index] = (uint8_t)(Index + 1);
    }
    WriteU16(Bytes + 48, 0x201);
    WriteU16(Bytes + 50, 44);
    Bytes[52] = SFB_TZ_SEMANTIC_SET_ROT;
    Bytes[53] = 2;

    WriteU16(Bytes + 56, 0x208);
    WriteU16(Bytes + 58, 64);
    Bytes[60] = SFB_TZ_SEMANTIC_SET_BOOTSTATE;
    Bytes[61] = 1;
    WriteU16(Bytes + 64, 0);
}

static void
AssertZeroMap (const SFB_TZ_MAP *Map)
{
    const uint8_t *Bytes = (const uint8_t *)Map;
    size_t Index;

    for (Index = 0; Index < sizeof(*Map); ++Index) {
        assert(Bytes[Index] == 0);
    }
}

static void
ExpectRejected (const uint8_t *Bytes, size_t Size)
{
    SFB_TZ_MAP Map;

    memset(&Map, 0xa5, sizeof(Map));
    assert(!SfbTzMapParse(Bytes, Size, &Map));
    AssertZeroMap(&Map);
}

static void
TestValidRoundTrip (void)
{
    uint8_t Bytes[SFB_TZMAP_BYTES + 1];
    uint8_t Reencoded[SFB_TZMAP_BYTES];
    SFB_TZ_MAP Map;
    SFB_TZ_MAP Again;

    MakeValid(Bytes);
    assert(SfbTzMapParse(Bytes, SFB_TZMAP_BYTES, &Map));
    assert(memcmp(Map.Magic, "GTZM", 4) == 0);
    assert(Map.Version == SFB_TZMAP_VERSION);
    assert(Map.CommandCount == 2);
    assert(Map.Flags == (SFB_TZMAP_FLAG_SPSS_CONSUMED |
                         SFB_TZMAP_FLAG_APP_KEYMASTER));
    assert(Map.Reserved0 == 0);
    for (size_t Index = 0; Index < sizeof(Map.AblDigest); ++Index) {
        assert(Map.AblDigest[Index] == (uint8_t)(Index + 1));
    }
    assert(Map.Commands[0].Command == 0x201);
    assert(Map.Commands[0].RequestBytes == 44);
    assert(Map.Commands[0].Semantic == SFB_TZ_SEMANTIC_SET_ROT);
    assert(Map.Commands[0].Occurrences == 2);
    assert(Map.Commands[0].Reserved == 0);
    assert(Map.Commands[1].Command == 0x208);
    assert(Map.Commands[1].RequestBytes == 64);
    assert(Map.Commands[1].Semantic == SFB_TZ_SEMANTIC_SET_BOOTSTATE);
    assert(Map.Commands[1].Occurrences == 1);
    assert(Map.Commands[1].Reserved == 0);
    assert(Map.Commands[2].Command == 0);
    assert(Map.Commands[15].Reserved == 0);
    for (size_t Index = 2; Index < SFB_TZMAP_MAX_COMMANDS; ++Index) {
        assert(Map.Commands[Index].Command == 0);
        assert(Map.Commands[Index].RequestBytes == 0);
        assert(Map.Commands[Index].Semantic == 0);
        assert(Map.Commands[Index].Occurrences == 0);
        assert(Map.Commands[Index].Reserved == 0);
    }
    for (size_t Index = 0; Index < sizeof(Map.Reserved1); ++Index) {
        assert(Map.Reserved1[Index] == 0);
    }

    SerializeMap(Reencoded, &Map);
    assert(memcmp(Reencoded, Bytes, SFB_TZMAP_BYTES) == 0);
    assert(SfbTzMapParse(Reencoded, SFB_TZMAP_BYTES, &Again));
    assert(memcmp(&Again, &Map, sizeof(Map)) == 0);
}

static void
TestRejections (void)
{
    uint8_t Bytes[SFB_TZMAP_BYTES + 1];

    MakeValid(Bytes);
    ExpectRejected(Bytes, SFB_TZMAP_BYTES - 1);
    ExpectRejected(Bytes, SFB_TZMAP_BYTES + 1);

    MakeValid(Bytes);
    Bytes[0] = 'X';
    ExpectRejected(Bytes, SFB_TZMAP_BYTES);

    MakeValid(Bytes);
    WriteU16(Bytes + 4, 0);
    ExpectRejected(Bytes, SFB_TZMAP_BYTES);
    MakeValid(Bytes);
    WriteU16(Bytes + 4, 2);
    ExpectRejected(Bytes, SFB_TZMAP_BYTES);

    MakeValid(Bytes);
    WriteU32(Bytes + 12, 1);
    ExpectRejected(Bytes, SFB_TZMAP_BYTES);

    MakeValid(Bytes);
    Bytes[176] = 1;
    ExpectRejected(Bytes, SFB_TZMAP_BYTES);
    MakeValid(Bytes);
    Bytes[255] = 1;
    ExpectRejected(Bytes, SFB_TZMAP_BYTES);

    MakeValid(Bytes);
    WriteU32(Bytes + 8, SFB_TZMAP_FLAG_ALL | 0x40);
    ExpectRejected(Bytes, SFB_TZMAP_BYTES);

    MakeValid(Bytes);
    WriteU16(Bytes + 6, 17);
    ExpectRejected(Bytes, SFB_TZMAP_BYTES);

    /* Semantic 9 is the highest accepted value; the parser stores it verbatim.
     * Firmware still resolves the acted-on semantic via SfbTzMapSemantic. */
    MakeValid(Bytes);
    Bytes[52] = 9;
    {
        SFB_TZ_MAP Map;
        assert(SfbTzMapParse(Bytes, SFB_TZMAP_BYTES, &Map));
        assert(Map.Commands[0].Semantic == SFB_TZ_SEMANTIC_GENERATE_FRS_UDS);
    }
    MakeValid(Bytes);
    Bytes[52] = 10;
    ExpectRejected(Bytes, SFB_TZMAP_BYTES);
    MakeValid(Bytes);
    WriteU16(Bytes + 54, 1);
    ExpectRejected(Bytes, SFB_TZMAP_BYTES);
    MakeValid(Bytes);
    WriteU16(Bytes + 48, 0);
    ExpectRejected(Bytes, SFB_TZMAP_BYTES);

    MakeValid(Bytes);
    WriteU16(Bytes + 56, 0x200);
    ExpectRejected(Bytes, SFB_TZMAP_BYTES);
    MakeValid(Bytes);
    WriteU16(Bytes + 56, 0x201);
    ExpectRejected(Bytes, SFB_TZMAP_BYTES);

    MakeValid(Bytes);
    Bytes[64] = 1;
    ExpectRejected(Bytes, SFB_TZMAP_BYTES);

    {
        SFB_TZ_MAP Map;
        memset(&Map, 0xa5, sizeof(Map));
        assert(!SfbTzMapParse(NULL, SFB_TZMAP_BYTES, &Map));
        AssertZeroMap(&Map);
    }
    MakeValid(Bytes);
    assert(!SfbTzMapParse(Bytes, SFB_TZMAP_BYTES, NULL));
}

static void
TestFind (void)
{
    uint8_t Bytes[SFB_TZMAP_BYTES + 1];
    SFB_TZ_MAP Map;

    MakeValid(Bytes);
    assert(SfbTzMapParse(Bytes, SFB_TZMAP_BYTES, &Map));
    assert(SfbTzMapFind(&Map, 0x201u) == &Map.Commands[0]);
    assert(SfbTzMapFind(&Map, 0x999u) == NULL);
    assert(SfbTzMapFind(&Map, 0x10201u) == NULL);
    assert(SfbTzMapFind(NULL, 0x201u) == NULL);
}

static void
TestBuiltinDefault (void)
{
    static const uint16_t Commands[] = {
        0x200, 0x201, 0x202, 0x203, 0x204, 0x207, 0x208, 0x211, 0x219
    };
    static const uint8_t Semantics[] = {
        SFB_TZ_SEMANTIC_GET_VERSION,
        SFB_TZ_SEMANTIC_SET_ROT,
        SFB_TZ_SEMANTIC_READ_DEVICE_STATE,
        SFB_TZ_SEMANTIC_WRITE_DEVICE_STATE,
        SFB_TZ_SEMANTIC_MILESTONE,
        SFB_TZ_SEMANTIC_SET_VERSION,
        SFB_TZ_SEMANTIC_SET_BOOTSTATE,
        SFB_TZ_SEMANTIC_SET_VBH,
        SFB_TZ_SEMANTIC_GENERATE_FRS_UDS
    };
    SFB_TZ_MAP Map;
    SFB_TZ_MAP Again;
    uint8_t Bytes[SFB_TZMAP_BYTES];
    size_t Index;

    SfbTzMapBuiltinDefault(&Map);
    assert(memcmp(Map.Magic, "GTZM", 4) == 0);
    assert(Map.Version == SFB_TZMAP_VERSION);
    assert(Map.Flags == SFB_TZMAP_FLAG_ALL);
    assert(Map.CommandCount == 9);
    for (Index = 0; Index < 9; ++Index) {
        assert(Map.Commands[Index].Command == Commands[Index]);
        assert(Map.Commands[Index].Semantic == Semantics[Index]);
        assert(Map.Commands[Index].Occurrences == 0);
        assert(Map.Commands[Index].Reserved == 0);
        if (Index != 0) {
            assert(Map.Commands[Index - 1].Command < Map.Commands[Index].Command);
        }
    }
    assert(Map.Commands[0].RequestBytes == 0);
    assert(Map.Commands[1].RequestBytes == 44);
    assert(Map.Commands[2].RequestBytes == 0);
    assert(Map.Commands[3].RequestBytes == 0);
    assert(Map.Commands[4].RequestBytes == 0);
    assert(Map.Commands[5].RequestBytes == 12);
    assert(Map.Commands[6].RequestBytes == 64);
    assert(Map.Commands[7].RequestBytes == 36);
    assert(Map.Commands[8].RequestBytes == 0);
    assert(SfbTzMapFind(&Map, 0x204u)->Semantic ==
           SFB_TZ_SEMANTIC_MILESTONE);
    assert(SfbTzMapFind(&Map, 0x219u)->Semantic ==
           SFB_TZ_SEMANTIC_GENERATE_FRS_UDS);
    assert(SfbTzMapSemantic(&Map, 0x204u) == SFB_TZ_SEMANTIC_MILESTONE);
    assert(SfbTzMapSemantic(&Map, 0x219u) ==
           SFB_TZ_SEMANTIC_GENERATE_FRS_UDS);

    SerializeMap(Bytes, &Map);
    assert(SfbTzMapParse(Bytes, SFB_TZMAP_BYTES, &Again));
    assert(memcmp(&Again, &Map, sizeof(Map)) == 0);

    SfbTzMapBuiltinDefault(NULL);
}

int
main (void)
{
    TestValidRoundTrip();
    TestRejections();
    TestFind();
    TestBuiltinDefault();
    puts("tzmap tests passed");
    return 0;
}
