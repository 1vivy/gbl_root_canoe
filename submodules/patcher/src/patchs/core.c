#include "patchs/core.h"
#include "patchs/libavb_force_success.h"

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

bool PatchBuffer(char *Data, int32_t Size) {
    PATCH10_RESULT Result;

    if (Data == NULL || Size <= 0) {
        return false;
    }

    Result = patch_libavb_force_success(Data, Size);
    if (Result != PATCH10_SUCCESS) {
        printf("Error: mandatory libavb_force_success patch failed (%d)\n",
               (int)Result);
        return false;
    }
    printf("libavb_force_success patch applied\n");
    if (patch_abl_gbl(Data, Size) != 0) {
        printf("Warning: Failed to patch ABL GBL\n");
    }
    return true;
}
