NDK := $(NDK_PATH)
CC := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android30-clang
CFLAGS := -O2

clean:
	rm -rf build || true

build_android: build/patch_partition_android build/mboot

build/patch_partition_android: patch_partition.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $<

build/mboot: build_mboot.sh
	sh build_mboot.sh $(NDK)

.PHONY: clean build_android
