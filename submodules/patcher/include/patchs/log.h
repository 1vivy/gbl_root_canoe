#ifndef CANOE_PATCH_LOG_H
#define CANOE_PATCH_LOG_H
#ifdef CANOE_PATCH_SILENT
#define PATCH_LOG(...) ((void)0)
#else
#include <stdio.h>
#define PATCH_LOG(...) printf(__VA_ARGS__)
#endif
#endif
