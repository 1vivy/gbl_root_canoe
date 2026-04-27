/*
 * Hook log macros — three-tier gating that respects the same DISABLE_PRINT /
 * DISABLE_PRINT_2 build flags used by LinuxLoader.c and QcomModulePkg.dsc.
 *
 *   KM_LOG_VERBOSE  — per-call intercepts, byte dumps, indirect-buffer probes.
 *                     Suppressed by DISABLE_PRINT (the high-volume gate).
 *   KM_LOG_INFO     — install confirmations, drops, important state changes.
 *                     Suppressed by DISABLE_PRINT_2 (the low-volume gate).
 *                     Lets a production build keep a one-line "yes the hook
 *                     ran" trace without the full byte-level dump.
 *   KM_LOG_ERROR    — install failures, fatal mismatches. Always on; the hook
 *                     is silently broken otherwise.
 *
 * UEFI-only — these macros expand to AsciiPrint(), which only exists inside
 * the EDK2 build. The host-side patcher (tools/patch_abl.c) never includes
 * the hook headers.
 */
#ifndef HOOK_LOG_H
#define HOOK_LOG_H

/* Disabled forms use `if (0)` so the compiler still sees argument references
 * — keeps -Wunused-but-set-variable / -Wunused-variable quiet for locals
 * computed solely for a log call. The `if (0)` body is dead-code-eliminated. */
#ifndef DISABLE_PRINT
#define KM_LOG_VERBOSE(fmt, ...) AsciiPrint(fmt, ##__VA_ARGS__)
#else
#define KM_LOG_VERBOSE(fmt, ...) do { if (0) AsciiPrint(fmt, ##__VA_ARGS__); } while (0)
#endif

#ifndef DISABLE_PRINT_2
#define KM_LOG_INFO(fmt, ...) AsciiPrint(fmt, ##__VA_ARGS__)
#else
#define KM_LOG_INFO(fmt, ...) do { if (0) AsciiPrint(fmt, ##__VA_ARGS__); } while (0)
#endif

#define KM_LOG_ERROR(fmt, ...) AsciiPrint(fmt, ##__VA_ARGS__)

#endif /* HOOK_LOG_H */
