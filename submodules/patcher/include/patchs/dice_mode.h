#ifndef PATCHS_DICE_MODE_H
#define PATCHS_DICE_MODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t Main;
    size_t Dummy;
} DICE_PLAN;

/* Preflight both BCC paths before any mandatory ABL patch writes. */
bool PlanDiceModeNormal(const char *Buffer, int32_t Size, DICE_PLAN *Plan);
void ApplyDiceModeNormal(char *Buffer, const DICE_PLAN *Plan);

#endif /* PATCHS_DICE_MODE_H */
