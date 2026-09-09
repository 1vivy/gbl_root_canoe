/* Unit tests for the lock-state fastboot gate patch.
 *
 * Fixtures are flat synthetic buffers rather than real PEs: the patch resolves
 * ADRP+ADD targets with load_base 0, so file offset == RVA and no PE header is
 * needed. Every encoding this file emits is fed back through the project's own
 * decoder in FixtureSelfCheck, so a hand-encoding mistake fails loudly instead
 * of silently testing the wrong instruction.
 */
#include "arm64_inst/utils.h"
#include "patchs/fastboot_lock_gates.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF_SIZE 0x4000
/* Where each refusal string lives. Page-crossing relative to the code at 0 so
 * the ADRP page delta is non-zero, as it is in a real ABL. */
#define STR_BASE 0x2000

static const char *const kGateStrings[] = {
    "Flashing is not allowed in Lock State",
    "Erase is not allowed in Lock State",
    "Slot Change is not allowed in Lock State\n",
    "Snapshot Cancel is not allowed in Lock State"
};
/* The decoder table has no entry producing INST_B, so an unconditional branch
 * decodes as INST_UNKNOWN. Verify it from the raw encoding instead. */
static bool IsUncondB(uint32_t Raw, int32_t *ByteDelta) {
    int32_t Imm26;

    if ((Raw & 0xFC000000u) != 0x14000000u) return false;
    Imm26 = (int32_t)(Raw & 0x03FFFFFFu);
    if (Imm26 & 0x02000000) Imm26 |= (int32_t)0xFC000000;
    *ByteDelta = Imm26 * 4;
    return true;
}

#define GATE_COUNT ((int32_t)(sizeof(kGateStrings) / sizeof(kGateStrings[0])))

static uint32_t EncodeAdrp(uint8_t Rd, int32_t InsnOff, int32_t TargetOff) {
    int32_t Pages = ((TargetOff & ~0xFFF) - (InsnOff & ~0xFFF)) >> 12;
    uint32_t Imm21 = (uint32_t)Pages & 0x1FFFFF;

    return 0x90000000u | ((Imm21 & 0x3u) << 29) | (((Imm21 >> 2) & 0x7FFFFu) << 5) | Rd;
}

static uint32_t EncodeAddXImm(uint8_t Rd, uint8_t Rn, uint32_t Imm12) {
    return 0x91000000u | ((Imm12 & 0xFFFu) << 10) | ((uint32_t)Rn << 5) | Rd;
}

/* imm19 is in instruction units; ByteDelta must be 4-aligned. */
static uint32_t EncodeBCond(uint32_t Cond, int32_t ByteDelta) {
    uint32_t Imm19 = ((uint32_t)(ByteDelta >> 2)) & 0x7FFFF;

    return 0x54000000u | (Imm19 << 5) | (Cond & 0xFu);
}

static uint32_t EncodeCbzW(uint8_t Rt, int32_t ByteDelta) {
    uint32_t Imm19 = ((uint32_t)(ByteDelta >> 2)) & 0x7FFFF;

    return 0x34000000u | (Imm19 << 5) | Rt;
}

static void PlaceGateString(char *Buf, int32_t Index) {
    strcpy(Buf + STR_BASE + Index * 0x100, kGateStrings[Index]);
}

/* Emit the ADRP+ADD pair that loads gate Index's string at CodeOff. */
static void PlaceAdrl(char *Buf, int32_t CodeOff, int32_t Index) {
    int32_t Target = STR_BASE + Index * 0x100;

    write_instr(Buf, CodeOff, EncodeAdrp(0, CodeOff, Target));
    write_instr(Buf, CodeOff + 4, EncodeAddXImm(0, 0, (uint32_t)(Target & 0xFFF)));
}

/* Prove the hand-rolled encoders agree with the decoder the patch relies on. */
static void FixtureSelfCheck(void) {
    char *Buf = calloc(1, BUF_SIZE);
    DecodedInst Inst;
    int64_t Resolved;
    int32_t Delta = 0;

    assert(Buf != NULL);
    PlaceGateString(Buf, 0);
    PlaceAdrl(Buf, 0x40, 0);
    Resolved = calc_adrl_file_offset(Buf, 0x40, 0);
    assert(Resolved == STR_BASE);
    assert(str_at(Buf, BUF_SIZE, Resolved, kGateStrings[0]));

    write_instr(Buf, 0x80, EncodeBCond(0x1, 0x40 - 0x80));
    Inst = decode_at(Buf, 0x80);
    assert(Inst.type == INST_BCOND);
    assert(Inst.cond == 0x1);
    assert(Inst.simm == 0x40 - 0x80);

    write_instr(Buf, 0x90, EncodeCbzW(3, 0x40 - 0x90));
    Inst = decode_at(Buf, 0x90);
    assert(Inst.type == INST_CBZ_W);
    assert(Inst.rt == 3);

    /* change_to_b must preserve the displacement while dropping the condition. */
    write_instr(Buf, 0xA0, EncodeBCond(0x1, -0x20));
    Inst = decode_at(Buf, 0xA0);
    write_instr(Buf, 0xA0, change_to_b(Inst.raw));
    assert(IsUncondB(read_instr(Buf, 0xA0), &Delta));
    assert(Delta == -0x20);

    free(Buf);
    printf("ok - fixture encodings agree with the decoder\n");
}

/* Pattern B: B.NE directly before the ADRP skips past the refusal. */
static void TestPatternB(void) {
    char *Buf = calloc(1, BUF_SIZE);
    int32_t Code = 0x100;
    int32_t Delta = 0;

    assert(Buf != NULL);
    for (int32_t i = 0; i < GATE_COUNT; i++) {
        int32_t Gate = Code + i * 0x40;

        PlaceGateString(Buf, i);
        /* B.NE +0x10 hops over the refusal block that starts at the ADRP. */
        write_instr(Buf, Gate, EncodeBCond(0x1, 0x14));
        PlaceAdrl(Buf, Gate + 4, i);
    }

    assert(patch_fastboot_lock_gates(Buf, BUF_SIZE) == LOCK_GATES_SUCCESS);

    for (int32_t i = 0; i < GATE_COUNT; i++) {
        int32_t Gate = Code + i * 0x40;

        assert(IsUncondB(read_instr(Buf, Gate), &Delta));  /* condition dropped */
        assert(Delta == 0x14);                             /* target preserved */
    }
    free(Buf);
    printf("ok - pattern B makes every lock gate unconditional\n");
}

/* Pattern A: a branch elsewhere jumps INTO the refusal block; NOP it. */
static void TestPatternA(void) {
    char *Buf = calloc(1, BUF_SIZE);

    assert(Buf != NULL);
    for (int32_t i = 0; i < GATE_COUNT; i++) {
        int32_t Gate = 0x800 + i * 0x40;
        int32_t Branch = 0x100 + i * 0x40;

        PlaceGateString(Buf, i);
        PlaceAdrl(Buf, Gate, i);
        /* Nothing decodable immediately before the ADRP, so Pattern B misses. */
        write_instr(Buf, Gate - 4, NOP);
        write_instr(Buf, Branch, EncodeCbzW(0, Gate - Branch));
    }

    assert(patch_fastboot_lock_gates(Buf, BUF_SIZE) == LOCK_GATES_SUCCESS);

    for (int32_t i = 0; i < GATE_COUNT; i++) {
        int32_t Branch = 0x100 + i * 0x40;

        assert(read_instr(Buf, Branch) == NOP);
    }
    free(Buf);
    printf("ok - pattern A NOPs the branch into each refusal block\n");
}

/* A build with none of the gates is reported, not failed, and left untouched. */
static void TestAbsent(void) {
    char *Buf = calloc(1, BUF_SIZE);
    char *Copy = calloc(1, BUF_SIZE);

    assert(Buf != NULL && Copy != NULL);
    strcpy(Buf + STR_BASE, "some unrelated loader string");
    write_instr(Buf, 0x100, EncodeBCond(0x1, 0x14));
    PlaceAdrl(Buf, 0x104, 0);
    memcpy(Copy, Buf, BUF_SIZE);

    assert(patch_fastboot_lock_gates(Buf, BUF_SIZE) == LOCK_GATES_ABSENT);
    assert(memcmp(Buf, Copy, BUF_SIZE) == 0);
    free(Buf);
    free(Copy);
    printf("ok - a build without lock gates is untouched and reported absent\n");
}

/* A duplicated anchor means the site is no longer uniquely identified. */
static void TestAmbiguousAnchor(void) {
    char *Buf = calloc(1, BUF_SIZE);
    char *Copy = calloc(1, BUF_SIZE);

    assert(Buf != NULL && Copy != NULL);
    PlaceGateString(Buf, 0);
    write_instr(Buf, 0x100, EncodeBCond(0x1, 0x14));
    PlaceAdrl(Buf, 0x104, 0);
    PlaceAdrl(Buf, 0x200, 0);   /* second reference to the same string */
    memcpy(Copy, Buf, BUF_SIZE);

    assert(patch_fastboot_lock_gates(Buf, BUF_SIZE) == LOCK_GATES_AMBIGUOUS);
    assert(memcmp(Buf, Copy, BUF_SIZE) == 0);
    free(Buf);
    free(Copy);
    printf("ok - an ambiguous anchor refuses and leaves the buffer alone\n");
}

/*
 * All-or-nothing. One resolvable gate plus one that is present but has no
 * recognisable gate must write nothing: a half-patched ABL would accept flash
 * and still refuse erase.
 */
static void TestPartialWritesNothing(void) {
    char *Buf = calloc(1, BUF_SIZE);
    char *Copy = calloc(1, BUF_SIZE);

    assert(Buf != NULL && Copy != NULL);
    /* Gate 0: resolvable via pattern B. */
    PlaceGateString(Buf, 0);
    write_instr(Buf, 0x100, EncodeBCond(0x1, 0x14));
    PlaceAdrl(Buf, 0x104, 0);
    /* Gate 1: present, but neither pattern applies — no branch reaches it. */
    PlaceGateString(Buf, 1);
    write_instr(Buf, 0x300, NOP);
    PlaceAdrl(Buf, 0x304, 1);
    memcpy(Copy, Buf, BUF_SIZE);

    assert(patch_fastboot_lock_gates(Buf, BUF_SIZE) == LOCK_GATES_FAILURE);
    assert(memcmp(Buf, Copy, BUF_SIZE) == 0);
    free(Buf);
    free(Copy);
    printf("ok - an unresolvable gate blocks the whole rewrite\n");
}

static void TestRejectsBadInput(void) {
    char Buf[16] = {0};

    assert(patch_fastboot_lock_gates(NULL, BUF_SIZE) == LOCK_GATES_FAILURE);
    assert(patch_fastboot_lock_gates(Buf, 0) == LOCK_GATES_FAILURE);
    assert(patch_fastboot_lock_gates(Buf, -1) == LOCK_GATES_FAILURE);
    printf("ok - bad input rejected\n");
}

int main(void) {
    FixtureSelfCheck();
    TestPatternB();
    TestPatternA();
    TestAbsent();
    TestAmbiguousAnchor();
    TestPartialWritesNothing();
    TestRejectsBadInput();
    printf("fastboot lock gate tests passed\n");
    return 0;
}
