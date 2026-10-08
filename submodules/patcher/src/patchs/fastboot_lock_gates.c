/* Lock-state fastboot gates.
 *
 * Port of gbl-chainload's patch6 (abl_permissive/fastboot_lock_gates). The
 * loader presents a locked VerifiedBoot view to the ABL; the ABL's in-fastboot
 * command dispatcher then honours that view and refuses flash, erase, slot
 * change and snapshot cancel. Each refusal has a command-specific message.
 * A nearby conditional edge either enters its message block or skips the
 * refusal. Rewriting those gates is what makes fastboot usable while the
 * device reports locked.
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

#include "patchs/log.h"
#include <string.h>

/*
 * Each anchor combines a short command prefix with the semantic state marker.
 * This tolerates wording and line-ending changes without mistaking the
 * unrelated "Slot Change ... merging state" refusal for a lock gate.
 */
static const char *const LockGateCommands[] = {
    "Flashing",
    "Erase",
    "Slot Change",
    "Snapshot Cancel"
};

#define LOCK_GATE_COUNT ((int32_t)(sizeof(LockGateCommands) / sizeof(LockGateCommands[0])))

/* B.NE — the allowed path on every Pattern B site observed. A different
 * condition means the layout is not the one this patch understands, so it
 * falls through to the Pattern A search rather than guessing. */
#define ARM64_COND_NE 0x1u
#define REFUSAL_ENTRY_COUNT 3
#define MAX_GATE_SPAN 0x1000
#define MAX_GATE_TEXT 128
#define LOCK_STATE_MARKER "Lock State"

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

static bool GateTextAt(const char *Buffer, int32_t Size, int64_t Target,
                       const char *Command) {
    size_t Start;
    size_t Available;
    size_t Length = 0;
    size_t CommandLength = strlen(Command);
    size_t MarkerLength = strlen(LOCK_STATE_MARKER);

    if (Target < 0 || Target >= Size) return false;
    Start = (size_t)Target;
    Available = (size_t)Size - Start;
    if (Available > MAX_GATE_TEXT) Available = MAX_GATE_TEXT;
    while (Length < Available && Buffer[Start + Length] != '\0') Length++;
    if (Length == Available || Length < CommandLength ||
        memcmp(Buffer + Start, Command, CommandLength) != 0) return false;
    for (size_t Index = CommandLength;
         Index + MarkerLength <= Length; ++Index) {
        if (memcmp(Buffer + Start + Index, LOCK_STATE_MARKER,
                   MarkerLength) == 0) return true;
    }
    return false;
}

/*
 * Locate the unique ADRP+ADD xref to a NUL-terminated gate message containing
 * both the command prefix and the Lock State discriminator.
 */
static int32_t FindGateAnchor(const char *Buffer, int32_t Size,
                              const char *Command, int32_t *Anchor) {
    int32_t Matches = 0;

    for (int32_t Offset = 0; Offset + 8 <= Size; Offset += 4) {
        int64_t Target = calc_adrl_file_offset(Buffer, Offset, 0);

        if (!GateTextAt(Buffer, Size, Target, Command)) continue;
        if (Matches == 0) *Anchor = Offset;
        Matches++;
    }
    return Matches;
}

static bool ConditionalBranchTarget(const char *Buffer, int32_t Offset,
                                    int64_t *Target) {
    DecodedInst Inst = decode_at(Buffer, Offset);
    uint32_t Raw;
    int32_t Immediate;

    if (IsConditionalBranch(Inst.type)) {
        return get_JUMP_target(&Inst, Offset, Target);
    }

    /* TBZ/TBNZ: b5:op:011011:imm14:Rt. The shared decoder does not expose
     * this family, but older ABL snapshot gates use TBZ. */
    Raw = read_instr(Buffer, Offset);
    if ((Raw & UINT32_C(0x7E000000)) != UINT32_C(0x36000000)) {
        return false;
    }
    Immediate = (int32_t)((Raw >> 5) & UINT32_C(0x3FFF));
    if ((Immediate & 0x2000) != 0) Immediate |= (int32_t)0xFFFFC000;
    *Target = (int64_t)Offset + (int64_t)Immediate * 4;
    return true;
}

/*
 * A refusal can load a format string or another argument immediately before
 * loading the anchored message. Walk back only across complete ADRP+ADD pairs;
 * arbitrary instructions are not accepted as part of the refusal entry.
 */
static int32_t RefusalEntries(const char *Buffer, int32_t Size, int32_t Anchor,
                              int32_t Entries[REFUSAL_ENTRY_COUNT]) {
    int32_t Count = 1;
    int32_t Entry = Anchor;

    Entries[0] = Anchor;
    while (Count < REFUSAL_ENTRY_COUNT && Entry >= 8) {
        int64_t Target = calc_adrl_file_offset(Buffer, Entry - 8, 0);
        if (Target < 0 || Target >= Size) break;
        Entry -= 8;
        Entries[Count++] = Entry;
    }
    return Count;
}

static bool IsEntry(const int32_t *Entries, int32_t Count, int64_t Target) {
    for (int32_t Index = 0; Index < Count; ++Index) {
        if (Target == Entries[Index]) return true;
    }
    return false;
}

/*
 * Find the sole nearby conditional branch entering the refusal prefix. The
 * bounded forward edge keeps an unrelated function from borrowing the string
 * anchor, while accepting CBZ/CBNZ, B.cond and legacy TBZ/TBNZ layouts.
 */
static int32_t FindBranchInto(const char *Buffer, int32_t Size,
                              const int32_t *Entries, int32_t EntryCount,
                              int32_t *Branch) {
    int32_t Matches = 0;
    int32_t FirstEntry = Entries[EntryCount - 1];
    int32_t Start = FirstEntry > MAX_GATE_SPAN
                        ? FirstEntry - MAX_GATE_SPAN
                        : 0;

    for (int32_t Offset = Start; Offset < FirstEntry; Offset += 4) {
        int64_t Target;
        if (Offset + 4 > Size ||
            !ConditionalBranchTarget(Buffer, Offset, &Target) ||
            !IsEntry(Entries, EntryCount, Target)) continue;
        if (Matches == 0) *Branch = Offset;
        Matches++;
    }
    return Matches;
}

static int32_t FindDirectSkip(const char *Buffer, int32_t Size,
                              const int32_t *Entries, int32_t EntryCount,
                              int32_t Anchor, int32_t *Branch,
                              uint32_t *Replacement) {
    int32_t Matches = 0;

    for (int32_t Index = 0; Index < EntryCount; ++Index) {
        int32_t Entry = Entries[Index];
        int64_t Target;
        DecodedInst Prior;
        if (Entry < 4) continue;
        Prior = decode_at(Buffer, Entry - 4);
        if (Prior.type != INST_BCOND || Prior.cond != ARM64_COND_NE ||
            !get_JUMP_target(&Prior, Entry - 4, &Target) ||
            Target < (int64_t)Anchor + 8 ||
            Target > (int64_t)Anchor + MAX_GATE_SPAN ||
            Target >= Size) continue;
        if (Matches == 0) {
            *Branch = Entry - 4;
            *Replacement = change_to_b(Prior.raw);
        }
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
                                     const char *Command, bool *Present,
                                     int32_t *RewriteOffset,
                                     uint32_t *RewriteValue) {
    int32_t Entries[REFUSAL_ENTRY_COUNT];
    int32_t EntryCount;
    int32_t Anchor = -1;
    int32_t Branch = -1;
    int32_t Matches;

    *Present = false;
    Matches = FindGateAnchor(Buffer, Size, Command, &Anchor);
    if (Matches == 0) {
        return LOCK_GATES_ABSENT;
    }
    *Present = true;
    if (Matches > 1) {
        PATCH_LOG("Warning: lock gate '%s' anchor matched %d times\n", Command,
                  (int)Matches);
        return LOCK_GATES_AMBIGUOUS;
    }
    EntryCount = RefusalEntries(Buffer, Size, Anchor, Entries);

    /* Pattern B: a B.NE immediately before a validated refusal entry skips
     * the refusal. Require its target to land just beyond the anchored block. */
    Matches = FindDirectSkip(Buffer, Size, Entries, EntryCount, Anchor,
                             &Branch, RewriteValue);
    if (Matches > 1) {
        PATCH_LOG("Warning: lock gate '%s' has %d direct skip branches\n",
                  Command, (int)Matches);
        return LOCK_GATES_AMBIGUOUS;
    }
    if (Matches == 1) {
        *RewriteOffset = Branch;
        return LOCK_GATES_SUCCESS;
    }

    /* Pattern A: one nearby conditional edge enters the validated prefix. */
    Matches = FindBranchInto(Buffer, Size, Entries, EntryCount, &Branch);
    if (Matches == 0) {
        return LOCK_GATES_FAILURE;
    }
    if (Matches > 1) {
        PATCH_LOG("Warning: lock gate '%s' has %d branches into its error block\n",
                  Command, (int)Matches);
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
            PlanOneGate(Buffer, Size, LockGateCommands[Index], &Present,
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
        PATCH_LOG("Patched lock-state gate at 0x%X (%s)\n", (unsigned)Offsets[Index],
               Values[Index] == NOP ? "NOP" : "unconditional B");
    }
    return LOCK_GATES_SUCCESS;
}
