#!/bin/sh
# 从 Magisk 官方源码编译 magiskboot（纯 C++ 版本 v24.3），输出到 build/mboot
set -e

NDK="$1"
MAGISK_VER="v24.3"
ROOT="$(pwd)"
SRC="$ROOT/build/magisk-src"

if [ -z "$NDK" ] || [ ! -f "$NDK/ndk-build" ]; then
  echo "[mboot] 未找到 ndk-build: $NDK" >&2
  exit 1
fi

# 拉取 Magisk 源码（含所需 submodule）
if [ ! -f "$SRC/native/jni/Android.mk" ]; then
  echo "[mboot] 拉取 Magisk $MAGISK_VER 源码..."
  rm -rf "$SRC"
  mkdir -p build
  git clone --depth 1 --branch "$MAGISK_VER" \
    https://github.com/topjohnwu/Magisk.git "$SRC"
  cd "$SRC"
  git submodule update --init --depth 1 \
    native/jni/external/libcxx \
    native/jni/external/lz4 \
    native/jni/external/bzip2 \
    native/jni/external/xz \
    native/jni/external/mincrypt \
    native/jni/external/dtc \
    native/jni/external/zlib
  cd "$ROOT"
fi

# 生成 build.py 通常会生成的 flags.h
mkdir -p "$SRC/native/out/generated"
cat > "$SRC/native/out/generated/flags.h" <<EOF
#pragma once
#define quote(s)            #s
#define str(s)              quote(s)
#define MAGISK_FULL_VER     MAGISK_VERSION "(" str(MAGISK_VER_CODE) ")"
#define NAME_WITH_VER(name) str(name) " " MAGISK_FULL_VER
#define MAGISK_VERSION      "$MAGISK_VER"
#define MAGISK_VER_CODE     24300
#define MAGISK_DEBUG        0
EOF

# 编译 magiskboot（仅 arm64 静态）
echo "[mboot] 编译 magiskboot..."
cd "$SRC/native"
"$NDK/ndk-build" B_BOOT=1 APP_ABI=arm64-v8a APP_PLATFORM=android-24 -j"$(nproc)"
cd "$ROOT"

cp "$SRC/native/libs/arm64-v8a/magiskboot" build/mboot
echo "[mboot] 完成: build/mboot"
