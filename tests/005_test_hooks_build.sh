#!/bin/bash
# Hook flow build smoke test. Cannot exercise the hook code without a
# Snapdragon target; this only catches build-side regressions:
#
#   1. make build_hooks_generic produces dist/hooks_generic.efi with a
#      valid MZ header.
#   2. Hook strings emitted via KM_LOG_INFO are actually in the binary
#      (catches ENABLE_KEYMASTER_HOOKS not propagating, hook headers
#      accidentally excluded, etc.).
#   3. A DISABLE_PRINT=1 DISABLE_PRINT_2=1 build is strictly smaller
#      than the verbose build (catches the if(0) DCE idiom in
#      tools/hook_log.h regressing).
set -e

cd "$(dirname "$0")/.."

# --- 1. Verbose build ---
make build_hooks_generic >/dev/null 2>&1
if [ ! -f dist/hooks_generic.efi ]; then
    echo "Test 005 failed: dist/hooks_generic.efi not produced by build_hooks_generic"
    exit 1
fi
GENERIC_SIZE=$(stat -c %s dist/hooks_generic.efi)
echo "  build_hooks_generic OK ($GENERIC_SIZE bytes)"

# --- 2. PE/COFF magic ---
MAGIC=$(head -c 2 dist/hooks_generic.efi | xxd -p)
if [ "$MAGIC" != "4d5a" ]; then
    echo "Test 005 failed: dist/hooks_generic.efi missing MZ header (got 0x$MAGIC)"
    exit 1
fi

# --- 3. Hook symbols compiled in ---
for S in "QseecomSendCmd hooked" "SCM hook: installed" "VerifiedBoot VBRwDeviceState hooked"; do
    if ! strings dist/hooks_generic.efi | grep -qF "$S"; then
        echo "Test 005 failed: hook string '$S' missing — ENABLE_KEYMASTER_HOOKS may not be wired"
        exit 1
    fi
done
echo "  hook symbols present in EFI"

# --- 4. Silent build shrinks output ---
make clean >/dev/null 2>&1
cp -r ./Conf ./edk2/
bash -c 'cd edk2 && . ./edksetup.sh && make BOARD_BOOTLOADER_PRODUCT_NAME=canoe TARGET_ARCHITECTURE=AARCH64 TARGET=RELEASE \
    CLANG_BIN=/usr/bin/ CLANG_PREFIX=aarch64-linux-gnu- VERIFIED_BOOT_ENABLED=1 \
    VERIFIED_BOOT_LE=0 AB_RETRYCOUNT_DISABLE=0 TARGET_BOARD_TYPE_AUTO=0 \
    BUILD_USES_RECOVERY_AS_BOOT=0 DISABLE_PARALLEL_DOWNLOAD_FLASH=0 PVMFW_BCC_ENABLED=-DPVMFW_BCC \
    REMOVE_CARVEOUT_REGION=1 QSPA_BOOTCONFIG_ENABLE=1 USER_BUILD_VARIANT=0 AUTO_PATCH_ABL=1 \
    ENABLE_KEYMASTER_HOOKS=1 DISABLE_PRINT=1 DISABLE_PRINT_2=1 \
    PREBUILT_HOST_TOOLS="BUILD_CC=clang BUILD_CXX=clang++ LDPATH=-fuse-ld=lld BUILD_AR=llvm-ar"' >/dev/null 2>&1
SILENT_EFI=edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi
if [ ! -f "$SILENT_EFI" ]; then
    echo "Test 005 failed: silent build produced no LinuxLoader.efi"
    exit 1
fi
SILENT_SIZE=$(stat -c %s "$SILENT_EFI")
if [ "$SILENT_SIZE" -ge "$GENERIC_SIZE" ]; then
    echo "Test 005 failed: silent build ($SILENT_SIZE) is not smaller than verbose ($GENERIC_SIZE) — DISABLE_PRINT gating regressed"
    exit 1
fi
echo "  silent build OK ($SILENT_SIZE bytes vs $GENERIC_SIZE verbose — DCE working)"

# --- cleanup ---
make clean >/dev/null 2>&1

echo "Test passed: hook build smoke checks pass."
