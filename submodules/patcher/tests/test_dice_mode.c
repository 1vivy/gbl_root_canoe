#include "patchs/dice_mode.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SIZE 0x1000
#define MAIN 0x420
#define DUMMY 0x618
#define LEGACY_MAIN 0x540
#define LEGACY_DUMMY 0x638

static void W16(uint8_t *At, uint16_t Value) {
    At[0] = (uint8_t)Value;
    At[1] = (uint8_t)(Value >> 8);
}
static void W32(uint8_t *At, uint32_t Value) {
    for (unsigned I = 0; I < 4; I++) At[I] = (uint8_t)(Value >> (I * 8));
}
static uint32_t R32(const uint8_t *At) {
    return (uint32_t)At[0] | ((uint32_t)At[1] << 8) |
           ((uint32_t)At[2] << 16) | ((uint32_t)At[3] << 24);
}

static void Fixture(uint8_t Image[SIZE]) {
    static const char MainLog[] = "VB: PopulateBccParams: Parameter receivedis NULL";
    static const char DummyLog[] = "VB: Setting Dummy DICE params\n";
    uint8_t *Section;
    memset(Image, 0, SIZE);
    Image[0] = 'M'; Image[1] = 'Z';
    W32(Image + 0x3c, 0x80);
    memcpy(Image + 0x80, "PE\0\0", 4);
    W16(Image + 0x84, 0xaa64);
    W16(Image + 0x86, 1);
    W16(Image + 0x94, 0xf0);
    W16(Image + 0x98, 0x20b);
    Section = Image + 0x188;
    memcpy(Section, ".text", 5);
    W32(Section + 8, 0xe00);
    W32(Section + 12, 0x1000);
    W32(Section + 16, 0xe00);
    W32(Section + 20, 0x200);
    W32(Section + 36, 0x60000020);
    memcpy(Image + 0x700, MainLog, sizeof(MainLog));
    memcpy(Image + 0x800, DummyLog, sizeof(DummyLog));
    W32(Image + 0x500, 0x90000001); /* ADRP X1, page 0x1000 */
    W32(Image + 0x504, 0x91140021); /* ADD X1, X1, #0x500 (main RVA) */
    W32(Image + 0x600, 0x90000001);
    W32(Image + 0x604, 0x91180021); /* ADD X1, X1, #0x600 (dummy RVA) */
    W32(Image + MAIN - 0x20, 0x1a8a0548); /* CINC W8,W10,NE */
    W32(Image + MAIN - 4, 0x52800069); /* MOV W9,#3 */
    W32(Image + MAIN, 0x1a890108); /* CSEL W8,W8,W9,EQ */
    W32(Image + MAIN + 4, 0x90000009);
    W32(Image + MAIN + 8, 0xb9000128); /* STR W8,[X9] */
    W32(Image + DUMMY - 4, 0x34000068); /* CBZ W8, past store */
    W32(Image + DUMMY, 0x52800048); /* MOV W8,#2 */
    W32(Image + DUMMY + 4, 0xb9006268); /* STR W8,[X19,#0x60] */
}

static void LegacyFixture(uint8_t Image[SIZE]) {
    Fixture(Image);

    /* Remove the newer sites while retaining their unique string xrefs. */
    W32(Image + MAIN - 0x20, 0);
    W32(Image + MAIN - 4, 0);
    W32(Image + MAIN, 0);
    W32(Image + MAIN + 4, 0);
    W32(Image + MAIN + 8, 0);
    W32(Image + DUMMY - 4, 0);
    W32(Image + DUMMY, 0);
    W32(Image + DUMMY + 4, 0);

    /*
     * Older ABLs derive Debug mode (2) when the state global equals either
     * 0x7070 or 0x77ee. The main path increments Normal (1); the dummy path
     * conditionally skips its Debug store. Both sites are after their xrefs.
     */
    W32(Image + LEGACY_MAIN - 0x1c, 0x90000008); /* ADRP X8,state */
    W32(Image + LEGACY_MAIN - 0x18, 0x528e0e09); /* MOV W9,#0x7070 */
    W32(Image + LEGACY_MAIN - 0x14, 0xb943fd08); /* LDR W8,[X8,#0x3fc] */
    W32(Image + LEGACY_MAIN - 0x10, 0x6b09011f); /* CMP W8,W9 */
    W32(Image + LEGACY_MAIN - 0x0c, 0x528efdc9); /* MOV W9,#0x77ee */
    W32(Image + LEGACY_MAIN - 0x08, 0x7a491104); /* CCMP W8,W9,#4,NE */
    W32(Image + LEGACY_MAIN - 0x04, 0x52800028); /* MOV W8,#1 */
    W32(Image + LEGACY_MAIN, 0x1a881508);        /* CINC W8,W8,EQ */
    W32(Image + LEGACY_MAIN + 0x04, 0x9107e3ea);
    W32(Image + LEGACY_MAIN + 0x08, 0x90000009); /* ADRP X9,BCC global */
    W32(Image + LEGACY_MAIN + 0x0c, 0x91001140);
    W32(Image + LEGACY_MAIN + 0x10, 0x2a1f03e1);
    W32(Image + LEGACY_MAIN + 0x14, 0x52800502);
    W32(Image + LEGACY_MAIN + 0x18, 0xb90ed928); /* STR W8,[X9,#imm] */

    W32(Image + LEGACY_DUMMY - 0x1c, 0x90000008); /* ADRP X8,state */
    W32(Image + LEGACY_DUMMY - 0x18, 0x528efdc9); /* MOV W9,#0x77ee */
    W32(Image + LEGACY_DUMMY - 0x14, 0xb943fd08); /* LDR W8,[X8,#0x3fc] */
    W32(Image + LEGACY_DUMMY - 0x10, 0x6b09011f); /* CMP W8,W9 */
    W32(Image + LEGACY_DUMMY - 0x0c, 0x528e0e09); /* MOV W9,#0x7070 */
    W32(Image + LEGACY_DUMMY - 0x08, 0x7a491104); /* CCMP W8,W9,#4,NE */
    W32(Image + LEGACY_DUMMY - 0x04, 0x54000061); /* B.NE past store */
    W32(Image + LEGACY_DUMMY, 0x52800048);        /* MOV W8,#2 */
    W32(Image + LEGACY_DUMMY + 4, 0xb9006268);    /* STR W8,[X19,#0x60] */
}

static void RejectsUnfamiliarShape(uint8_t Image[SIZE]) {
    uint8_t Before[SIZE];
    DICE_PLAN Plan;
    memcpy(Before, Image, SIZE);
    assert(!PlanDiceModeNormal((const char *)Image, SIZE, &Plan));
    assert(memcmp(Before, Image, SIZE) == 0);
}

int main(void) {
    uint8_t Image[SIZE], Before[SIZE];
    DICE_PLAN Plan;
    Fixture(Image);
    memcpy(Before, Image, SIZE);
    assert(PlanDiceModeNormal((const char *)Image, SIZE, &Plan));
    assert(Plan.Main == MAIN && Plan.Dummy == DUMMY);
    ApplyDiceModeNormal((char *)Image, &Plan);
    assert(R32(Image + MAIN) == 0x52800028);
    assert(R32(Image + DUMMY - 4) == 0xd503201f);
    assert(R32(Image + DUMMY) == 0x52800028);
    /* Three code words only; the anchors and all other instructions survive. */
    W32(Image + MAIN, R32(Before + MAIN));
    W32(Image + DUMMY - 4, R32(Before + DUMMY - 4));
    W32(Image + DUMMY, R32(Before + DUMMY));
    assert(memcmp(Image, Before, SIZE) == 0);

    LegacyFixture(Image);
    memcpy(Before, Image, SIZE);
    assert(PlanDiceModeNormal((const char *)Image, SIZE, &Plan));
    assert(Plan.Main == LEGACY_MAIN && Plan.Dummy == LEGACY_DUMMY);
    ApplyDiceModeNormal((char *)Image, &Plan);
    assert(R32(Image + LEGACY_MAIN) == 0x52800028);
    assert(R32(Image + LEGACY_DUMMY - 4) == 0xd503201f);
    assert(R32(Image + LEGACY_DUMMY) == 0x52800028);
    W32(Image + LEGACY_MAIN, R32(Before + LEGACY_MAIN));
    W32(Image + LEGACY_DUMMY - 4, R32(Before + LEGACY_DUMMY - 4));
    W32(Image + LEGACY_DUMMY, R32(Before + LEGACY_DUMMY));
    assert(memcmp(Image, Before, SIZE) == 0);

    LegacyFixture(Image);
    W32(Image + LEGACY_MAIN - 0x18, 0x52824689); /* Unknown state tag. */
    RejectsUnfamiliarShape(Image);
    LegacyFixture(Image);
    W32(Image + LEGACY_DUMMY - 4, 0x54000081); /* Branch past more than store. */
    RejectsUnfamiliarShape(Image);
    LegacyFixture(Image);
    W32(Image + LEGACY_DUMMY + 4, 0xd503201f); /* No dummy BCC store. */
    RejectsUnfamiliarShape(Image);

    Fixture(Image);
    W32(Image + DUMMY - 4, 0x34000088); /* Branch no longer skips store. */
    RejectsUnfamiliarShape(Image);
    Fixture(Image);
    W32(Image + MAIN + 8, 0xd503201f); /* No BCC-global store. */
    RejectsUnfamiliarShape(Image);
    Fixture(Image);
    memcpy(Image + 0x900, Image + 0x700,
           sizeof("VB: PopulateBccParams: Parameter receivedis NULL"));
    RejectsUnfamiliarShape(Image);
    Fixture(Image);
    W32(Image + 0x504, 0x91140020); /* ADD X0,X1 is not an ADRP pair. */
    RejectsUnfamiliarShape(Image);
    puts("DICE Normal patch tests passed");
    return 0;
}
