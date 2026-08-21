#ifndef PATCHS_LIBAVB_FORCE_SUCCESS_H
#define PATCHS_LIBAVB_FORCE_SUCCESS_H

#include <stdint.h>

typedef enum {
    LIBAVB_FORCE_FAILURE = 0,
    LIBAVB_FORCE_SUCCESS = 1,
    LIBAVB_FORCE_AMBIGUOUS = 2
} LIBAVB_FORCE_RESULT;

/* Force libavb verification success, or leave the buffer unchanged on any
 * failed preflight. */
LIBAVB_FORCE_RESULT patch_libavb_force_success(char *Buffer, int32_t Size);

#endif /* PATCHS_LIBAVB_FORCE_SUCCESS_H */
