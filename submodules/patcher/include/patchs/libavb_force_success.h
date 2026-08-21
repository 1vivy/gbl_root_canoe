#ifndef PATCHS_LIBAVB_FORCE_SUCCESS_H
#define PATCHS_LIBAVB_FORCE_SUCCESS_H

#include <stdint.h>

typedef enum {
    PATCH10_FAILURE = 0,
    PATCH10_SUCCESS = 1,
    PATCH10_AMBIGUOUS = 2
} PATCH10_RESULT;

/* Apply donor patch10, or leave the buffer unchanged on any failed preflight. */
PATCH10_RESULT patch_libavb_force_success(char *Buffer, int32_t Size);

#endif /* PATCHS_LIBAVB_FORCE_SUCCESS_H */
