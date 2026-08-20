#!/bin/sh
# 从 Magisk v28.1 源码编译 magiskboot（含 Rust boot-rs + 官方 ONDK r27.4 工具链），
# 并把 vendor_boot 修补逻辑（patch.cpp）集成进同一二进制。
# 输出 build/patch_tools（内置 unpack/cpio/repack，支持 vendor_boot header v4）。
set -e

MAGISK_VER="v28.1"
ONDK_VER="r27.4"
ROOT="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
BUILD="$ROOT/build"
SRC="$BUILD/magisk-src"
ONDK="$BUILD/ondk"

# 1) 下载并解压 ONDK（Magisk 官方定制 NDK，自带匹配版本的 nightly Rust 工具链）
if [ ! -x "$ONDK/ndk-build" ]; then
  echo "[patch_tools] 下载 ONDK $ONDK_VER ..."
  mkdir -p "$BUILD"
  wget -q --timeout=120 --tries=3 \
    "https://github.com/topjohnwu/ondk/releases/download/$ONDK_VER/ondk-$ONDK_VER-linux.tar.xz" \
    -O "$BUILD/ondk.tar.xz.tmp"
  mv "$BUILD/ondk.tar.xz.tmp" "$BUILD/ondk.tar.xz"
  tar -xf "$BUILD/ondk.tar.xz" -C "$BUILD"
  mv "$BUILD/ondk-$ONDK_VER" "$ONDK"
  rm -f "$BUILD/ondk.tar.xz"
fi

# 2) 拉取 Magisk 源码（含 magiskboot 所需的 submodule）
if [ ! -f "$SRC/native/src/Android.mk" ]; then
  echo "[patch_tools] 拉取 Magisk $MAGISK_VER 源码..."
  rm -rf "$SRC"
  git clone --depth 1 --branch "$MAGISK_VER" \
    https://github.com/topjohnwu/Magisk.git "$SRC"
  cd "$SRC"
  git submodule update --init --depth 1 --jobs "$JOBS" \
    native/src/external/libcxx \
    native/src/external/lz4 \
    native/src/external/bzip2 \
    native/src/external/xz \
    native/src/external/zlib \
    native/src/external/zopfli \
    native/src/external/cxx-rs \
    native/src/external/crt0 \
    native/src/external/selinux \
    native/src/external/lsplt \
    native/src/external/system_properties
  cd "$ROOT"
fi

JNI="$SRC/native/src"

# 3) 注入 patch.cpp（vendor_boot 修补逻辑）
cp "$ROOT/patch.cpp" "$JNI/boot/patch.cpp"

# 4) Android.mk 增加 patch.cpp 源文件（带续行符）
if ! grep -q 'boot/patch.cpp' "$JNI/Android.mk"; then
  sed -i 's|    boot/main.cpp|    boot/patch.cpp \\\n    boot/main.cpp|' "$JNI/Android.mk"
fi

# 5) main.cpp 增加 patch_vendor 子命令
if ! grep -q 'action == "patch_vendor"' "$JNI/boot/main.cpp"; then
  sed -i 's|    if (action == "cleanup") {|    if (argc > 2 \&\& action == "patch_vendor") {\n        return patch_vendor_boot(argc - 2, argv + 2);\n    } else if (action == "cleanup") {|' "$JNI/boot/main.cpp"
fi

# 6) magiskboot.hpp 增加声明
if ! grep -q 'int patch_vendor_boot' "$JNI/boot/magiskboot.hpp"; then
  sed -i '/int split_image_dtb(const char \*filename, bool skip_decomp = false);/ a\int patch_vendor_boot(int argc, char *argv[]);' "$JNI/boot/magiskboot.hpp"
fi

# 7) 生成 flags.h（等价于 build.py 的 dump_flag_header）
mkdir -p "$SRC/native/out/generated"
cat > "$SRC/native/out/generated/flags.h" <<FLAGS_EOF
#pragma once
#define quote(s)            #s
#define str(s)              quote(s)
#define MAGISK_FULL_VER     MAGISK_VERSION "(" str(MAGISK_VER_CODE) ")"
#define NAME_WITH_VER(name) str(name) " " MAGISK_FULL_VER
#define MAGISK_VERSION      "$MAGISK_VER"
#define MAGISK_VER_CODE     28100
#define MAGISK_DEBUG        0
FLAGS_EOF

# 8) 编译 Rust boot-rs（cargo build -p magiskboot）
echo "[patch_tools] 编译 Rust boot-rs ..."
cd "$JNI"
export PATH="$ONDK/toolchains/rust/bin:$PATH"
export CARGO_BUILD_RUSTC="$ONDK/toolchains/rust/bin/rustc"
export CARGO_BUILD_RUSTFLAGS="-Z threads=$JOBS"
"$ONDK/toolchains/rust/bin/cargo" build -p magiskboot -r --target aarch64-linux-android
mkdir -p "$SRC/native/out/arm64-v8a"
cp "$SRC/native/out/rust/aarch64-linux-android/release/libmagiskboot.a" \
   "$SRC/native/out/arm64-v8a/libmagiskboot-rs.a"

# 9) 编译 magiskboot（C++ + Rust 静态链接，含 patch_vendor）
echo "[patch_tools] 编译 magiskboot（含 patch_vendor）..."
cd "$SRC/native"
"$ONDK/ndk-build" B_BOOT=1 B_CRT0=1 NDK_PROJECT_PATH=. NDK_APPLICATION_MK=src/Application.mk APP_ABI=arm64-v8a -j"$JOBS"
cd "$ROOT"

mkdir -p build
cp "$SRC/native/libs/arm64-v8a/magiskboot" build/patch_tools
"$ONDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" build/patch_tools 2>/dev/null || true
ls -lh build/patch_tools
echo "[patch_tools] 完成: build/patch_tools"
