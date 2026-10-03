#include "patchs/dice_mode.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SIZE 0x1000
#define MAIN 0x420
#define DUMMY 0x618

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
