#include "patchs/core.h"
#include "patchs/libavb_force_success.h"
#include "patchs/fastboot_lock_gates.h"
#include "patchs/oplus/forceenablefastboot.h"
#include "patchs/oplus/warning.h"

#include "arm64_inst/utils.h"

#include <stdio.h>
#include <string.h>

static int32_t patch_abl_gbl(char *Buffer, int32_t Size) {
    static const char Target[] = {'e', 0, 'f', 0, 'i', 0, 's', 0, 'p', 0};
    static const char Replacement[] = {'n', 0, 'u', 0, 'l', 0, 'l', 0, 's', 0};
    const int32_t TargetLength = (int32_t)sizeof(Target);

    for (int32_t Index = 0; Index <= Size - TargetLength; ++Index) {
        if (memcmp(Buffer + Index, Target, sizeof(Target)) == 0) {
            memcpy(Buffer + Index, Replacement, sizeof(Replacement));
            return 0;
        }
    }
    return -1;
}

/*
 * Boot-state comparison signature. -1 is a wildcard byte.
 *
 * This is the same pattern the retired boot-state patch matched, but it is used
 * here for LOCATION ONLY: the lock-state register number and the anchor offset
 * are what the Oplus warning patch needs to prove a candidate branch really is
 * fed by the lock-state variable. Nothing in this function writes to Buffer —
 * rewriting the comparison is the runtime VerifiedBoot hook's job now, and the
 * host tests assert this signature stays byte-identical.
 */
static const int16_t BootStateSignature[] = {
    -1, 0x00, 0x00, 0x34, 0x28, 0x00, 0x80, 0x52,
    0x06, 0x00, 0x00, 0x14, 0xE8, -1, 0x40, 0xF9,
    0x08, 0x01, 0x40, 0x39, 0x1F, 0x01, 0x00, 0x71,
    0xE8, 0x07, 0x9F, 0x1A, 0x08, 0x79, 0x1F, 0x53
};

/*
 * Locate the single boot-state comparison. Returns the number of matches; the
 * caller treats anything other than exactly one as "do not trust the result",
 * because a second match means the signature no longer identifies one site.
 */
static int32_t LocateBootState(const char *Buffer, int32_t Size,
                               int8_t *LockRegister, int32_t *Offset) {
    const int32_t PatternLength =
        (int32_t)(sizeof(BootStateSignature) / sizeof(BootStateSignature[0]));
    int32_t Found = 0;

    if (Buffer == NULL || LockRegister == NULL || Offset == NULL ||
        Size < PatternLength) {
        return 0;
    }
    for (int32_t Index = 0; Index <= Size - PatternLength; ++Index) {
        bool Match = true;
        for (int32_t Byte = 0; Byte < PatternLength; ++Byte) {
            if (BootStateSignature[Byte] != -1 &&
                (uint8_t)Buffer[Index + Byte] !=
                    (uint8_t)BootStateSignature[Byte]) {
                Match = false;
                break;
            }
        }
        if (!Match) {
            continue;
        }
        *LockRegister = (int8_t)((uint8_t)Buffer[Index] & 0x1F);
        *Offset = Index;
        Found++;
        Index += PatternLength - 1;
    }
    return Found;
}

/*
 * Resolve the lock-state global the boot-state comparison reads. The traversal
 * uses empty_source_callback, so it observes the LDRB chain without rewriting
 * any of it; the retired source/sink rewrites are deliberately not restored.
 */
static bool LocateLockStateGlobal(char *Buffer, int32_t Size,
                                  int32_t *GlobalVarOffset) {
    int32_t Offset = -1;
    int8_t LockRegister = -1;
    int32_t Matches;

    *GlobalVarOffset = -1;
    Matches = LocateBootState(Buffer, Size, &LockRegister, &Offset);
    if (Matches != 1) {
        printf("Warning: boot-state signature matched %d times, "
               "skipping Oplus warning patch\n",
               (int)Matches);
        return false;
    }
    if (find_ldrB_instructio_reverse(Buffer, Size, Offset, LockRegister,
                                     GlobalVarOffset,
                                     empty_source_callback) != 0 ||
        *GlobalVarOffset < 0) {
        printf("Warning: lock-state global not resolved for W%d\n",
               (int)LockRegister);
        return false;
    }
    printf("Lock-state global offset: 0x%X (register W%d)\n",
           (unsigned)*GlobalVarOffset, (int)LockRegister);
    return true;
}

bool PatchBuffer(char *Data, int32_t Size) {
    LIBAVB_FORCE_RESULT Result;
    int32_t GlobalVarOffset = -1;

    if (Data == NULL || Size <= 0) {
        return false;
    }

    Result = patch_libavb_force_success(Data, Size);
    if (Result != LIBAVB_FORCE_SUCCESS) {
        printf("Error: mandatory libavb_force_success patch failed (%d)\n",
               (int)Result);
        return false;
    }
    printf("libavb_force_success patch applied\n");
    if (patch_abl_gbl(Data, Size) != 0) {
        printf("Warning: Failed to patch ABL GBL\n");
    }

    /*
     * Lock-state fastboot gates. The loader hands the ABL a locked view, so
     * without this the ABL refuses flash / erase / slot change / snapshot
     * cancel. Best effort rather than mandatory: losing it costs fastboot
     * write access, which is a usability regression, not an unbootable device,
     * and an ABL that ships none of these gates is a legitimate outcome.
     */
    switch (patch_fastboot_lock_gates(Data, Size)) {
        case LOCK_GATES_SUCCESS:
            printf("fastboot lock-state gates patched\n");
            break;
        case LOCK_GATES_ABSENT:
            printf("Warning: no fastboot lock-state gates found; fastboot may "
                   "refuse flash while the device reports locked\n");
            break;
        default:
            printf("Warning: fastboot lock-state gate patch failed; fastboot "
                   "will refuse flash while the device reports locked\n");
            break;
    }

    /*
     * Oplus cosmetic/usability patches. Both are best effort: the loader still
     * boots without them, so a signature that no longer matches on a future
     * build warns rather than failing the whole patch.
     */
    if (LocateLockStateGlobal(Data, Size, &GlobalVarOffset)) {
        if (!patch_warning(Data, Size, GlobalVarOffset)) {
            printf("Warning: Oplus orange-state warning patch failed\n");
        }
    }
    if (!patch_fastboot(Data, Size, GlobalVarOffset)) {
        printf("Warning: Oplus force-enable-fastboot patch failed\n");
    }

    return true;
}
