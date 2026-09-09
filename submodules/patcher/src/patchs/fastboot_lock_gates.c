/* Lock-state fastboot gates.
 *
 * Port of gbl-chainload's patch6 (abl_permissive/fastboot_lock_gates). The
 * loader presents a locked VerifiedBoot view to the ABL; the ABL's in-fastboot
 * command dispatcher then honours that view and refuses flash, erase, slot
 * change and snapshot cancel. Each refusal has its own message, and each
 * message is loaded by an ADRP+ADD pair immediately governed by the gate that
 * decides whether the refusal runs. Rewriting those gates is what makes
 * fastboot usable while the device reports locked.
 *
 * Two shapes occur, and which one a build uses depends only on how the
 * compiler laid out the basic blocks:
 *
 *   Pattern B — a conditional branch sits directly before the ADRP and jumps
 *     PAST the error block on the allowed path. Make it unconditional so the
 *     refusal is skipped every time.
 *
 *   Pattern A — a conditional branch elsewhere jumps INTO the error block.
 *     NOP it so control falls through to the allowed path.
 *
 * Vendor-neutral: the messages are ABL strings, not Oplus additions, which is
 * why this sits beside libavb_force_success rather than under oplus/. A build
 * that ships none of them is reported as ABSENT so the caller can treat it as
 * "nothing to do" rather than a failure.
 */
#include "patchs/fastboot_lock_gates.h"
#include "arm64_inst/utils.h"

#include <stdio.h>

/* Every gate the ABL guards behind lock state. The trailing newline on the
 * slot-change message is part of the literal in the binary; matching without it
 * would still hit, but keeping it exact keeps the anchor unique. */
static const char *const LockGateStrings[] = {
    "Flashing is not allowed in Lock State",
    "Erase is not allowed in Lock State",
    "Slot Change is not allowed in Lock State\n",
    "Snapshot Cancel is not allowed in Lock State"
};

#define LOCK_GATE_COUNT ((int32_t)(sizeof(LockGateStrings) / sizeof(LockGateStrings[0])))

/* B.NE — the allowed path on every Pattern B site observed. A different
 * condition means the layout is not the one this patch understands, so it
 * falls through to the Pattern A search rather than guessing. */
#define ARM64_COND_NE 0x1u

static bool IsConditionalBranch(InstType Type) {
    return Type == INST_BCOND || Type == INST_CBZ_W || Type == INST_CBZ_X ||
           Type == INST_CBNZ_W || Type == INST_CBNZ_X;
}

/* Not idempotent, by choice. Once a Pattern A branch is NOPed the evidence that
 * a gate was ever there is gone, and treating "nothing branches into the
 * refusal block" as already-patched would be wrong: a block reached by
 * straight-line fallthrough has no inbound branch either, and reporting success
 * there would leave the refusal live. A Pattern A site also commonly has the
 * previous basic block ending in an unconditional B right before the ADRP, so
 * that cannot be used as an already-patched marker either. Every caller in this
 * tree patches a freshly extracted LinuxLoader.efi, so the input is pristine. */

/*
 * Locate the ADRP that begins the ADRP+ADD pair loading Needle. Returns the
 * number of matches; anything above one means the anchor no longer identifies a
 * single site and the caller must refuse rather than pick.
 */
static int32_t FindGateAnchor(const char *Buffer, int32_t Size,
                              const char *Needle, int32_t *Anchor) {
    int32_t Matches = 0;

    for (int32_t Offset = 0; Offset + 8 <= Size; Offset += 4) {
        int64_t Target = calc_adrl_file_offset(Buffer, Offset, 0);

        if (Target < 0) continue;
        if (!str_at(Buffer, Size, Target, Needle)) continue;
        if (Matches == 0) *Anchor = Offset;
        Matches++;
    }
    return Matches;
}

/*
 * Find the sole conditional branch jumping to Anchor. Uniqueness matters as
 * much as existence: two branches into the same error block means NOPing one
 * leaves the other refusal path live, so the gate is reported unresolved.
 */
static int32_t FindBranchInto(const char *Buffer, int32_t Size, int32_t Anchor,
                              int32_t *Branch) {
    int32_t Matches = 0;

    for (int32_t Offset = 0; Offset + 4 <= Size; Offset += 4) {
        DecodedInst Inst = decode_at(Buffer, Offset);
        int64_t Target = 0;

        if (!IsConditionalBranch(Inst.type)) continue;
        if (!get_JUMP_target(&Inst, Offset, &Target)) continue;
        if (Target != (int64_t)Anchor) continue;
        if (Matches == 0) *Branch = Offset;
        Matches++;
    }
    return Matches;
}

/*
 * Resolve one gate. Reports where the rewrite goes without performing it, so
 * the caller can require every gate to be resolvable before touching the
 * buffer. *Present distinguishes "this build has no such gate" from "the gate
 * is here and could not be understood".
 */
static LOCK_GATES_RESULT PlanOneGate(const char *Buffer, int32_t Size,
                                     const char *Needle, bool *Present,
                                     int32_t *RewriteOffset,
                                     uint32_t *RewriteValue) {
    int32_t Anchor = -1;
    int32_t Branch = -1;
    int32_t Matches;
    DecodedInst Prior;

    *Present = false;
    Matches = FindGateAnchor(Buffer, Size, Needle, &Anchor);
    if (Matches == 0) {
        return LOCK_GATES_ABSENT;
    }
    *Present = true;
    if (Matches > 1) {
        printf("Warning: lock gate '%s' anchor matched %d times\n", Needle,
               (int)Matches);
        return LOCK_GATES_AMBIGUOUS;
    }
    if (Anchor < 4) {
        return LOCK_GATES_FAILURE;
    }

    /* Pattern B. */
    Prior = decode_at(Buffer, Anchor - 4);
    if (Prior.type == INST_BCOND && Prior.cond == ARM64_COND_NE) {
        *RewriteOffset = Anchor - 4;
        *RewriteValue = change_to_b(Prior.raw);
        return LOCK_GATES_SUCCESS;
    }

    /* Pattern A. */
    Matches = FindBranchInto(Buffer, Size, Anchor, &Branch);
    if (Matches == 0) {
        return LOCK_GATES_FAILURE;
    }
    if (Matches > 1) {
        printf("Warning: lock gate '%s' has %d branches into its error block\n",
               Needle, (int)Matches);
        return LOCK_GATES_AMBIGUOUS;
    }
    *RewriteOffset = Branch;
    *RewriteValue = NOP;
    return LOCK_GATES_SUCCESS;
}

LOCK_GATES_RESULT patch_fastboot_lock_gates(char *Buffer, int32_t Size) {
    int32_t Offsets[LOCK_GATE_COUNT];
    uint32_t Values[LOCK_GATE_COUNT];
    int32_t Planned = 0;

    if (Buffer == NULL || Size <= 0) {
        return LOCK_GATES_FAILURE;
    }

    /* Plan every gate first. A build where one gate is understood and another
     * is not would otherwise be left half-rewritten: fastboot would accept
     * flash but still refuse erase, which is worse than refusing both. */
    for (int32_t Index = 0; Index < LOCK_GATE_COUNT; Index++) {
        bool Present = false;
        int32_t RewriteOffset = -1;
        uint32_t RewriteValue = 0;
        LOCK_GATES_RESULT Result =
            PlanOneGate(Buffer, Size, LockGateStrings[Index], &Present,
                        &RewriteOffset, &RewriteValue);

        if (Result == LOCK_GATES_ABSENT) continue;
        if (Result != LOCK_GATES_SUCCESS) {
            return Result;
        }
        Offsets[Planned] = RewriteOffset;
        Values[Planned] = RewriteValue;
        Planned++;
    }

    if (Planned == 0) {
        return LOCK_GATES_ABSENT;
    }

    for (int32_t Index = 0; Index < Planned; Index++) {
        write_instr(Buffer, Offsets[Index], Values[Index]);
        printf("Patched lock-state gate at 0x%X (%s)\n", (unsigned)Offsets[Index],
               Values[Index] == NOP ? "NOP" : "unconditional B");
    }
    return LOCK_GATES_SUCCESS;
}
