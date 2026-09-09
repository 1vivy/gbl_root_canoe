#ifndef PATCHS_FASTBOOT_LOCK_GATES_H
#define PATCHS_FASTBOOT_LOCK_GATES_H

#include <stdint.h>

typedef enum {
    LOCK_GATES_FAILURE = 0,
    LOCK_GATES_SUCCESS = 1,
    LOCK_GATES_AMBIGUOUS = 2,
    LOCK_GATES_ABSENT = 3
} LOCK_GATES_RESULT;

/* Neutralise the in-fastboot lock-state gates so flash / erase / slot-change /
 * snapshot-cancel are accepted while the ABL believes the device is locked.
 * All-or-nothing: a buffer is only modified once every gate present in it has
 * been resolved, so a partial rewrite is never handed back. */
LOCK_GATES_RESULT patch_fastboot_lock_gates(char *Buffer, int32_t Size);

#endif /* PATCHS_FASTBOOT_LOCK_GATES_H */
