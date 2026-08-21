#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "patchs/core.h"
#include "patchs/libavb_force_success.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Pull the private writer into this host-only test without adding a
 * production-visible API or changing the package-facing entry point. */
#define main patch_abl_test_entrypoint
#include "../src/patch_abl.c"
#undef main

#define IMAGE_SIZE 0x900
#define CODE_OFFSET 0x200
#define MANY_SECTIONS 97
#define MANY_SECTIONS_IMAGE_SIZE 0x1500
#define MANY_SECTIONS_CODE_OFFSET 0x1100
#define MANY_SECTIONS_ANCHOR_OFFSET 0x1300
#define ANCHOR_OFFSET 0x600
#define RETIRED_BOOT_STATE_OFFSET 0x700
#define RETIRED_ADRL_OFFSET 0x740
#define RETIRED_WARNING_OFFSET 0x7b0
#define RETIRED_SOURCE_SINK_OFFSET 0x800
#define RETIRED_FASTBOOT_OFFSET 0x820
#define RETIRED_FASTBOOT_BRANCH_OFFSET 0x850

static const uint8_t Anchor[] =
    "Persistent values required for AVB_HASHTREE_ERROR_MODE_MANAGED_RESTART_AND_EIO";
static const uint8_t Efisp[] = {'e', 0, 'f', 0, 'i', 0, 's', 0, 'p', 0};
static const uint8_t Nulls[] = {'n', 0, 'u', 0, 'l', 0, 'l', 0, 's', 0};

/* These are deliberately embedded in the same valid PE fixture as patch10.
 * They model every retired family from the old pipeline and act as mutation
 * sentinels: none is an allowed patch target anymore. */
/* Retired ADRL unlock-to-lock: ADRP+ADD triples plus their strings. */
static const uint8_t RetiredAdrl[] = {
    0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00, 0x91,
    0x01, 0x00, 0x00, 0x90, 0x21, 0x00, 0x01, 0x91,
    0x02, 0x00, 0x00, 0x90, 0x42, 0x00, 0x02, 0x91,
};
static const uint8_t RetiredAdrlUnlocked[] = "unlocked";
static const uint8_t RetiredAdrlLocked[] = "locked";
static const uint8_t RetiredAdrlDeviceState[] = "androidboot.vbmeta.device_state";
/* Retired wildcard boot-state patch. */
static const uint8_t RetiredBootState[] = {
    0x08, 0x00, 0x00, 0x34, 0x28, 0x00, 0x80, 0x52,
    0x06, 0x00, 0x00, 0x14, 0xe8, 0x00, 0x40, 0xf9,
    0x08, 0x01, 0x40, 0x39, 0x1f, 0x01, 0x00, 0x71,
    0xe8, 0x07, 0x9f, 0x1a, 0x08, 0x79, 0x1f, 0x53,
};
/* Retired OPlus warning patch. */
static const uint8_t RetiredWarningOrange[] = "Orange State\n";
static const uint8_t RetiredWarningUnlocked[] =
    "Your device has been unlocked and can't be trusted\n";
/* Retired source/sink tracking patch: source LDRB, source MOV, sink STRB. */
static const uint8_t RetiredSourceSink[] = {
    0x08, 0x00, 0x40, 0x39, 0x28, 0x00, 0x80, 0x52,
    0x08, 0x00, 0x00, 0x39, 0x08, 0x04, 0x00, 0x39,
};
/* Retired force-fastboot patch. */
static const uint8_t RetiredFastbootString[] =
    "fastboot_unlock_verify error and reboot.";
static const uint8_t RetiredFastbootBranch[] = {
    0x40, 0x00, 0x00, 0x34,
};

static void WriteU16(uint8_t *Data, uint16_t Value) {
    Data[0] = (uint8_t)Value;
    Data[1] = (uint8_t)(Value >> 8);
}

static void WriteU32(uint8_t *Data, uint32_t Value) {
    Data[0] = (uint8_t)Value;
    Data[1] = (uint8_t)(Value >> 8);
    Data[2] = (uint8_t)(Value >> 16);
    Data[3] = (uint8_t)(Value >> 24);
}

static uint32_t ReadU32(const uint8_t *Data) {
    return (uint32_t)Data[0] |
           ((uint32_t)Data[1] << 8) |
           ((uint32_t)Data[2] << 16) |
           ((uint32_t)Data[3] << 24);
}

static void SeedRetiredSignatures(uint8_t Image[IMAGE_SIZE]) {
    memcpy(Image + RETIRED_BOOT_STATE_OFFSET, RetiredBootState,
           sizeof(RetiredBootState));
    memcpy(Image + RETIRED_ADRL_OFFSET, RetiredAdrl, sizeof(RetiredAdrl));
    memcpy(Image + RETIRED_ADRL_OFFSET + sizeof(RetiredAdrl),
           RetiredAdrlUnlocked, sizeof(RetiredAdrlUnlocked));
    memcpy(Image + RETIRED_ADRL_OFFSET + sizeof(RetiredAdrl) + 0x10,
           RetiredAdrlLocked, sizeof(RetiredAdrlLocked));
    memcpy(Image + RETIRED_ADRL_OFFSET + sizeof(RetiredAdrl) + 0x20,
           RetiredAdrlDeviceState, sizeof(RetiredAdrlDeviceState));
    memcpy(Image + RETIRED_WARNING_OFFSET, RetiredWarningOrange,
           sizeof(RetiredWarningOrange));
    memcpy(Image + RETIRED_WARNING_OFFSET + 0x10, RetiredWarningUnlocked,
           sizeof(RetiredWarningUnlocked));
    memcpy(Image + RETIRED_SOURCE_SINK_OFFSET, RetiredSourceSink,
           sizeof(RetiredSourceSink));
    memcpy(Image + RETIRED_FASTBOOT_OFFSET, RetiredFastbootString,
           sizeof(RetiredFastbootString));
    memcpy(Image + RETIRED_FASTBOOT_BRANCH_OFFSET, RetiredFastbootBranch,
           sizeof(RetiredFastbootBranch));
}

static void AssertRetiredSignaturesUnchanged(const uint8_t *Before,
                                             const uint8_t *After) {
    assert(memcmp(Before + RETIRED_BOOT_STATE_OFFSET,
                  After + RETIRED_BOOT_STATE_OFFSET,
                  sizeof(RetiredBootState)) == 0);
    assert(memcmp(Before + RETIRED_ADRL_OFFSET,
                  After + RETIRED_ADRL_OFFSET,
                  sizeof(RetiredAdrl)) == 0);
    assert(memcmp(Before + RETIRED_ADRL_OFFSET + sizeof(RetiredAdrl),
                  After + RETIRED_ADRL_OFFSET + sizeof(RetiredAdrl),
                  sizeof(RetiredAdrlUnlocked)) == 0);
    assert(memcmp(Before + RETIRED_ADRL_OFFSET + sizeof(RetiredAdrl) + 0x10,
                  After + RETIRED_ADRL_OFFSET + sizeof(RetiredAdrl) + 0x10,
                  sizeof(RetiredAdrlLocked)) == 0);
    assert(memcmp(Before + RETIRED_ADRL_OFFSET + sizeof(RetiredAdrl) + 0x20,
                  After + RETIRED_ADRL_OFFSET + sizeof(RetiredAdrl) + 0x20,
                  sizeof(RetiredAdrlDeviceState)) == 0);
    assert(memcmp(Before + RETIRED_WARNING_OFFSET,
                  After + RETIRED_WARNING_OFFSET,
                  sizeof(RetiredWarningOrange)) == 0);
    assert(memcmp(Before + RETIRED_WARNING_OFFSET + 0x10,
                  After + RETIRED_WARNING_OFFSET + 0x10,
                  sizeof(RetiredWarningUnlocked)) == 0);
    assert(memcmp(Before + RETIRED_SOURCE_SINK_OFFSET,
                  After + RETIRED_SOURCE_SINK_OFFSET,
                  sizeof(RetiredSourceSink)) == 0);
    assert(memcmp(Before + RETIRED_FASTBOOT_OFFSET,
                  After + RETIRED_FASTBOOT_OFFSET,
                  sizeof(RetiredFastbootString)) == 0);
    assert(memcmp(Before + RETIRED_FASTBOOT_BRANCH_OFFSET,
                  After + RETIRED_FASTBOOT_BRANCH_OFFSET,
                  sizeof(RetiredFastbootBranch)) == 0);
}

static void MakeFixture(uint8_t Image[IMAGE_SIZE], bool Executable) {
    uint8_t *Section;

    memset(Image, 0, IMAGE_SIZE);
    Image[0] = 'M';
    Image[1] = 'Z';
    WriteU32(Image + 0x3c, 0x80);
    memcpy(Image + 0x80, "PE\0\0", 4);
    WriteU16(Image + 0x84, 0xaa64);
    WriteU16(Image + 0x86, 1);
    WriteU16(Image + 0x94, 0xf0);
    WriteU16(Image + 0x98, 0x20b);

    Section = Image + 0x188;
    memcpy(Section, ".text", 5);
    WriteU32(Section + 8, 0x700);
    WriteU32(Section + 12, 0x1000);
    WriteU32(Section + 16, 0x700);
    WriteU32(Section + 20, CODE_OFFSET);
    WriteU32(Section + 36, Executable ? 0x60000020 : 0x40000040);

    WriteU32(Image + CODE_OFFSET, 0xd503233f);
    WriteU32(Image + CODE_OFFSET + 4, 0x2a0303e5);
    WriteU32(Image + CODE_OFFSET + 8, 0x90000000);
    WriteU32(Image + CODE_OFFSET + 12, 0x91100000);
    WriteU32(Image + CODE_OFFSET + 16, 0x2a0703e0);
    WriteU32(Image + CODE_OFFSET + 20, 0xd65f03c0);
    memcpy(Image + ANCHOR_OFFSET, Anchor, sizeof(Anchor) - 1);
}

static void MakeManySectionsFixture(
    uint8_t Image[MANY_SECTIONS_IMAGE_SIZE]) {
    uint8_t *Section;

    memset(Image, 0, MANY_SECTIONS_IMAGE_SIZE);
    Image[0] = 'M';
    Image[1] = 'Z';
    WriteU32(Image + 0x3c, 0x80);
    memcpy(Image + 0x80, "PE\0\0", 4);
    WriteU16(Image + 0x84, 0xaa64);
    WriteU16(Image + 0x86, MANY_SECTIONS);
    WriteU16(Image + 0x94, 0xf0);
    WriteU16(Image + 0x98, 0x20b);

    Section = Image + 0x188;
    WriteU32(Section + 8, 0x400);
    WriteU32(Section + 12, 0x1000);
    WriteU32(Section + 16, 0x400);
    WriteU32(Section + 20, MANY_SECTIONS_CODE_OFFSET);
    WriteU32(Section + 36, 0x60000020);

    WriteU32(Image + MANY_SECTIONS_CODE_OFFSET, 0xd503233f);
    WriteU32(Image + MANY_SECTIONS_CODE_OFFSET + 4, 0x2a0303e5);
    WriteU32(Image + MANY_SECTIONS_CODE_OFFSET + 8, 0x90000000);
    WriteU32(Image + MANY_SECTIONS_CODE_OFFSET + 12, 0x91080000);
    WriteU32(Image + MANY_SECTIONS_CODE_OFFSET + 16, 0x2a0703e0);
    WriteU32(Image + MANY_SECTIONS_CODE_OFFSET + 20, 0xd65f03c0);
    memcpy(Image + MANY_SECTIONS_ANCHOR_OFFSET, Anchor, sizeof(Anchor) - 1);
}

static void TestExactPatchWords(void) {
    // Given: one bounded executable xref to one patch10 anchor.
    uint8_t Image[IMAGE_SIZE];
    MakeFixture(Image, true);

    // When: mandatory patch10 is applied.
    assert(patch_libavb_force_success((char *)Image, IMAGE_SIZE) == PATCH10_SUCCESS);

    // Then: only the two selected instructions carry the exact donor words.
    assert(ReadU32(Image + CODE_OFFSET + 4) == 0x32000065);
    assert(ReadU32(Image + CODE_OFFSET + 16) == 0x52800000);
}

static void TestBackscanCrossesEarlierRet(void) {
    uint8_t Image[IMAGE_SIZE];
    MakeFixture(Image, true);

    // A valid function may lay out an early-return block before the anchor xref.
    WriteU32(Image + CODE_OFFSET + 8, 0x2a0703e0);
    WriteU32(Image + CODE_OFFSET + 12, 0xd65f03c0);
    WriteU32(Image + CODE_OFFSET + 16, 0x90000000);
    WriteU32(Image + CODE_OFFSET + 20, 0x91100000);
    WriteU32(Image + CODE_OFFSET + 24, 0xd503201f);

    assert(patch_libavb_force_success((char *)Image, IMAGE_SIZE) ==
           PATCH10_SUCCESS);
    assert(ReadU32(Image + CODE_OFFSET + 4) == 0x32000065);
    assert(ReadU32(Image + CODE_OFFSET + 8) == 0x52800000);
}

/* Rd==31 is WZR: `MOV WZR, W3` discards the value, so rewriting it to
 * `ORR WZR, W3, #1` would be a no-op that still reported success for a
 * MANDATORY patch, shipping an ABL that still enforces AVB. The matcher must
 * skip it and keep scanning for a real destination register. */
static void TestWzrDestinationIsRejected(void) {
    uint8_t Image[IMAGE_SIZE];
    MakeFixture(Image, true);

    // Given: a degenerate MOV WZR, W3 before the real MOV W5, W3. The ADRP+ADD
    // anchor xref at +8/+12 must stay intact, so the real target and the
    // trailing MOV W0/RET move down by two instructions.
    WriteU32(Image + CODE_OFFSET + 4, 0x2a0303ff);
    WriteU32(Image + CODE_OFFSET + 16, 0x2a0303e5);
    WriteU32(Image + CODE_OFFSET + 20, 0x2a0703e0);
    WriteU32(Image + CODE_OFFSET + 24, 0xd65f03c0);

    assert(patch_libavb_force_success((char *)Image, IMAGE_SIZE) ==
           PATCH10_SUCCESS);

    // Then: WZR is untouched and the real destination register was patched.
    assert(ReadU32(Image + CODE_OFFSET + 4) == 0x2a0303ff);
    assert(ReadU32(Image + CODE_OFFSET + 16) == 0x32000065);
    assert(ReadU32(Image + CODE_OFFSET + 20) == 0x52800000);
}

static void AssertFailureLeavesImageUnchanged(uint8_t Image[IMAGE_SIZE],
                                              PATCH10_RESULT Expected,
                                              const char *Case) {
    uint8_t Before[IMAGE_SIZE];
    PATCH10_RESULT Actual;
    memcpy(Before, Image, IMAGE_SIZE);
    Actual = patch_libavb_force_success((char *)Image, IMAGE_SIZE);
    if (Actual != Expected) {
        fprintf(stderr, "%s: expected patch10 result %d, got %d\n",
                Case, Expected, Actual);
    }
    assert(Actual == Expected);
    assert(memcmp(Image, Before, IMAGE_SIZE) == 0);
}

static void TestFailedPreflightsAreAtomic(void) {
    uint8_t Image[IMAGE_SIZE];

    // Given/When/Then: a non-executable reference is rejected without writes.
    MakeFixture(Image, false);
    AssertFailureLeavesImageUnchanged(Image, PATCH10_FAILURE, "non-executable");

    // A 32-bit ADD cannot form the required ADRP+ADD address reference.
    MakeFixture(Image, true);
    WriteU32(Image + CODE_OFFSET + 12, 0x11100000);
    AssertFailureLeavesImageUnchanged(Image, PATCH10_FAILURE, "32-bit ADD");

    // A shifted ADD resolves a different address and must not be misdecoded.
    MakeFixture(Image, true);
    WriteU32(Image + CODE_OFFSET + 12, 0x91500000);
    AssertFailureLeavesImageUnchanged(Image, PATCH10_FAILURE, "shifted ADD");

    // The two mandatory writes may not target the same MOV W0,W3 instruction.
    MakeFixture(Image, true);
    WriteU32(Image + CODE_OFFSET + 4, 0x2a0303e0);
    WriteU32(Image + CODE_OFFSET + 16, 0xd503201f);
    AssertFailureLeavesImageUnchanged(Image, PATCH10_FAILURE, "overlapping MOV");

    // A unique anchor outside every mapped section is still a hard failure.
    MakeFixture(Image, true);
    WriteU32(Image + 0x188 + 16, 0x300);
    AssertFailureLeavesImageUnchanged(Image, PATCH10_FAILURE, "unmapped anchor");

    // Executable section bytes may not overlap the PE headers/section table.
    MakeFixture(Image, true);
    WriteU32(Image + 0x188 + 20, 0x180);
    AssertFailureLeavesImageUnchanged(Image, PATCH10_FAILURE, "header overlap");

    // A missing anchor is atomic at the patch10 boundary, not only in PatchBuffer.
    MakeFixture(Image, true);
    memset(Image + ANCHOR_OFFSET, 0, sizeof(Anchor) - 1);
    AssertFailureLeavesImageUnchanged(Image, PATCH10_FAILURE, "missing anchor");

    // Given/When/Then: a duplicate executable reference is ambiguous and atomic.
    MakeFixture(Image, true);
    WriteU32(Image + CODE_OFFSET + 0x40, 0x90000001);
    WriteU32(Image + CODE_OFFSET + 0x44, 0x91100021);
    AssertFailureLeavesImageUnchanged(Image, PATCH10_AMBIGUOUS, "duplicate xref");

    // Given/When/Then: duplicate anchors are ambiguous and atomic.
    MakeFixture(Image, true);
    memcpy(Image + ANCHOR_OFFSET + 0x80, Anchor, sizeof(Anchor) - 1);
    AssertFailureLeavesImageUnchanged(Image, PATCH10_AMBIGUOUS, "duplicate anchor");

    // The MOV-from-W3 must be in the same function before its first RET.
    MakeFixture(Image, true);
    WriteU32(Image + CODE_OFFSET + 4, 0xd503201f);
    WriteU32(Image + CODE_OFFSET + 24, 0x2a0303e5);
    AssertFailureLeavesImageUnchanged(Image, PATCH10_FAILURE,
                                     "MOV after function RET");


    // A missing RET cannot borrow the next function's epilogue.
    MakeFixture(Image, true);
    WriteU32(Image + CODE_OFFSET + 20, 0xd503201f);
    WriteU32(Image + CODE_OFFSET + 24, 0xd503233f);
    WriteU32(Image + CODE_OFFSET + 28, 0xd65f03c0);
    AssertFailureLeavesImageUnchanged(Image, PATCH10_FAILURE,
                                     "RET across next PACIASP");

    // A non-AArch64 PE must not be treated as an ARM64 instruction image.
    MakeFixture(Image, true);
    WriteU16(Image + 0x84, 0x8664);
    AssertFailureLeavesImageUnchanged(Image, PATCH10_FAILURE, "wrong PE machine");

    // ARM64 PE32 is rejected; patch10 only accepts PE32+ images.
    MakeFixture(Image, true);
    WriteU16(Image + 0x98, 0x10b);
    AssertFailureLeavesImageUnchanged(Image, PATCH10_FAILURE, "PE32 image");

    // Section-count rejection happens before the quadratic overlap checks.
    {
        uint8_t ManySections[MANY_SECTIONS_IMAGE_SIZE];
        uint8_t Before[MANY_SECTIONS_IMAGE_SIZE];
        MakeManySectionsFixture(ManySections);
        memcpy(Before, ManySections, sizeof(Before));
        assert(patch_libavb_force_success(
                   (char *)ManySections, MANY_SECTIONS_IMAGE_SIZE) ==
               PATCH10_FAILURE);
        assert(memcmp(ManySections, Before, sizeof(ManySections)) == 0);
    }

    // A magic-only optional header is truncated even if a section table follows.
    MakeFixture(Image, true);
    WriteU16(Image + 0x94, 2);
    AssertFailureLeavesImageUnchanged(Image, PATCH10_FAILURE,
                                     "truncated optional header");

    // Given/When/Then: malformed PE input is rejected without writes.
    memset(Image, 0xa5, sizeof(Image));
    AssertFailureLeavesImageUnchanged(Image, PATCH10_FAILURE, "malformed PE");
}

static void TestPipelineKeepsOnlySelectedPatches(void) {
    uint8_t Image[IMAGE_SIZE];
    uint8_t Before[IMAGE_SIZE];

    // Given: both efisp markers and every retired patch family in one valid PE.
    MakeFixture(Image, true);
    SeedRetiredSignatures(Image);
    memcpy(Image + 0x680, Efisp, sizeof(Efisp));
    memcpy(Image + 0x6a0, Efisp, sizeof(Efisp));
    memcpy(Before, Image, sizeof(Before));

    // When: the package-facing PatchBuffer pipeline runs.
    assert(PatchBuffer((char *)Image, IMAGE_SIZE));

    // Then: efisp is first-match only, patch10 is mandatory, and retired bytes stay exact.
    assert(memcmp(Image + 0x680, Nulls, sizeof(Nulls)) == 0);
    assert(memcmp(Image + 0x6a0, Efisp, sizeof(Efisp)) == 0);
    assert(ReadU32(Image + CODE_OFFSET + 4) == 0x32000065);
    assert(ReadU32(Image + CODE_OFFSET + 16) == 0x52800000);
    AssertRetiredSignaturesUnchanged(Before, Image);

    // Given/When/Then: missing efisp stays nonfatal when mandatory patch10 succeeds.
    MakeFixture(Image, true);
    assert(PatchBuffer((char *)Image, IMAGE_SIZE));


    // Given/When/Then: missing patch10 is a hard, whole-pipeline atomic failure.
    MakeFixture(Image, true);
    memcpy(Image + 0x680, Efisp, sizeof(Efisp));
    memset(Image + ANCHOR_OFFSET, 0, sizeof(Anchor) - 1);
    memcpy(Before, Image, sizeof(Before));
    assert(!PatchBuffer((char *)Image, IMAGE_SIZE));
    assert(memcmp(Image, Before, sizeof(Image)) == 0);
}
static void WriteTestFile(const char *Path, const char *Data) {
    FILE *File = fopen(Path, "wb");
    assert(File != NULL);
    assert(fwrite(Data, 1, strlen(Data), File) == strlen(Data));
    assert(fclose(File) == 0);
}

static void AssertTestFile(const char *Path, const char *Expected) {
    char *Data = NULL;
    int32_t Size = 0;
    assert(read_file(Path, &Data, &Size) == 0);
    assert((size_t)Size == strlen(Expected));
    assert(memcmp(Data, Expected, strlen(Expected)) == 0);
    free(Data);
}

static void TestAtomicWriterCandidates(void) {
    char Output[256];
    char Candidate[256];
    char SymlinkPath[256];
    char Target[256];
    struct stat CandidateStat;
    unsigned long Pid = process_id();
    unsigned int Attempt;

    assert(snprintf(Output, sizeof(Output), "/tmp/patcher-atomic-%lu.out",
                    Pid) > 0);
    assert(snprintf(Target, sizeof(Target), "/tmp/patcher-atomic-%lu.target",
                    Pid) > 0);
    assert(snprintf(SymlinkPath, sizeof(SymlinkPath),
                    "/tmp/patcher-atomic-%lu.symlink", Pid) > 0);
    remove(Output);
    remove(Target);
    remove(SymlinkPath);
    for (Attempt = 0; Attempt < ATOMIC_TEMP_ATTEMPTS; ++Attempt) {
        assert(snprintf(Candidate, sizeof(Candidate),
                        "%s.tmp.%lu.%u", Output, Pid, Attempt) > 0);
        remove(Candidate);
    }

    // Existing regular and symlink candidates are never followed or truncated.
    WriteTestFile(Output, "old");
    assert(snprintf(Candidate, sizeof(Candidate), "%s.tmp.%lu.%u", Output,
                    Pid, 0u) > 0);
    WriteTestFile(Candidate, "candidate");
    WriteTestFile(Target, "target");
    assert(symlink(Target, SymlinkPath) == 0);
    assert(snprintf(Candidate, sizeof(Candidate), "%s.tmp.%lu.%u", Output,
                    Pid, 1u) > 0);
    assert(rename(SymlinkPath, Candidate) == 0);
    assert(write_file_atomic(Output, "new", 3) == 0);
    AssertTestFile(Output, "new");
    assert(lstat(Candidate, &CandidateStat) == 0);
    assert(S_ISLNK(CandidateStat.st_mode));
    AssertTestFile(Target, "target");
    assert(snprintf(Candidate, sizeof(Candidate), "%s.tmp.%lu.%u", Output,
                    Pid, 0u) > 0);
    AssertTestFile(Candidate, "candidate");

    // Exhausting the bounded candidates leaves the existing destination intact.
    remove(Output);
    for (Attempt = 0; Attempt < ATOMIC_TEMP_ATTEMPTS; ++Attempt) {
        assert(snprintf(Candidate, sizeof(Candidate),
                        "%s.tmp.%lu.%u", Output, Pid, Attempt) > 0);
        remove(Candidate);
    }
    WriteTestFile(Output, "preserved");
    for (Attempt = 0; Attempt < ATOMIC_TEMP_ATTEMPTS; ++Attempt) {
        assert(snprintf(Candidate, sizeof(Candidate),
                        "%s.tmp.%lu.%u", Output, Pid, Attempt) > 0);
        WriteTestFile(Candidate, "collision");
    }
    assert(write_file_atomic(Output, "replacement", 11) != 0);
    AssertTestFile(Output, "preserved");

    remove(Output);
    remove(Target);
    remove(SymlinkPath);
    for (Attempt = 0; Attempt < ATOMIC_TEMP_ATTEMPTS; ++Attempt) {
        assert(snprintf(Candidate, sizeof(Candidate),
                        "%s.tmp.%lu.%u", Output, Pid, Attempt) > 0);
        remove(Candidate);
    }
}

int main(void) {
    TestExactPatchWords();
    TestBackscanCrossesEarlierRet();
    TestWzrDestinationIsRejected();
    TestFailedPreflightsAreAtomic();
    TestPipelineKeepsOnlySelectedPatches();
    TestAtomicWriterCandidates();
    puts("patcher host tests passed");
    return 0;
}
