#ifndef PATCHS_CORE_H
#define PATCHS_CORE_H
#include <stdint.h>
#include <stdbool.h>
#define PATCH_REQUIRED_AVB 1u
#define PATCH_EFISP_REDIRECT 2u
#define PATCH_FASTBOOT_GATES 4u
#define PATCH_OPLUS_WARNING 8u
#define PATCH_OPLUS_FASTBOOT 16u
uint32_t PatchBufferFlags(char* data, int32_t size);
bool PatchBuffer(char* data, int32_t size);
#endif /* PATCHS_CORE_H */