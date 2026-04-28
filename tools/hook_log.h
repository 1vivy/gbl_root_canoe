/*
 * Hook log macros — three-tier output, gated by the DISABLE_PRINT /
 * DISABLE_PRINT_2 build flags shared with LinuxLoader.c and
 * QcomModulePkg.dsc.
 *
 * End-state behavior across the make targets:
 *
 *   build_hooks_generic (DISABLE_PRINT=0 DISABLE_PRINT_2=0):
 *     all three tiers go to AsciiPrint → very verbose splash output,
 *     used during hook iteration and probe builds.
 *
 *   build / build_hooks (DISABLE_PRINT=1 DISABLE_PRINT_2=1):
 *     KM_LOG_VERBOSE: compiled out (args still evaluated for warnings).
 *     KM_LOG_INFO:    DEBUG((EFI_D_INFO, ...)) — UefiLog only, no splash.
 *     KM_LOG_ERROR:   AsciiPrint — always painted on splash, regardless
 *                     of either flag. If something fails the user must
 *                     see it.
 *
 *   In short: generic = noisy on splash. build/build_hooks = clean splash
 *   unless something errors.
 *
 * UEFI-only — these macros expand to AsciiPrint() / DEBUG() which only
 * exist inside the EDK2 build. The host-side patcher (tools/patch_abl.c)
 * never includes the hook headers.
 */
#ifndef HOOK_LOG_H
#define HOOK_LOG_H

#include <Library/DebugLib.h>

/* High-volume per-call traces (request/response dumps, byte-level probes).
 * Disabled form uses `if (0)` so the compiler still sees argument
 * references — keeps -Wunused-but-set-variable / -Wunused-variable quiet
 * for locals computed solely for a log call. The `if (0)` body is
 * dead-code-eliminated. */
#ifndef DISABLE_PRINT
#define KM_LOG_VERBOSE(fmt, ...) AsciiPrint(fmt, ##__VA_ARGS__)
#else
#define KM_LOG_VERBOSE(fmt, ...) do { if (0) AsciiPrint(fmt, ##__VA_ARGS__); } while (0)
#endif

/* Low-volume informational events: hook installs, drops, important
 * state transitions. When DISABLE_PRINT_2=1 these fall through to
 * EDK2's debug framework instead of dropping entirely, so a production
 * build still records them in UefiLog without painting the splash. */
#ifndef DISABLE_PRINT_2
#define KM_LOG_INFO(fmt, ...)  AsciiPrint(fmt, ##__VA_ARGS__)
#else
#define KM_LOG_INFO(fmt, ...)  DEBUG((EFI_D_INFO,  fmt, ##__VA_ARGS__))
#endif

/* Install failures and fatal mismatches. Always AsciiPrint, never gated
 * — if a hook can't install or a verify-write fails, that has to be
 * visible on the splash regardless of DISABLE_PRINT / DISABLE_PRINT_2. */
#define KM_LOG_ERROR(fmt, ...) AsciiPrint(fmt, ##__VA_ARGS__)

#endif /* HOOK_LOG_H */
