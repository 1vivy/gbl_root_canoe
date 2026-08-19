NDK := $(NDK_PATH)
CC := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android30-clang
CFLAGS := -O2

clean:
	rm -rf build || true

build_android:
	mkdir -p build
	$(CC) $(CFLAGS) -o build/patch_super_android patch_super.c
	$(CC) $(CFLAGS) -o build/patch_vendor_boot_android patch_vendor_boot.c
	cp mboot build/mboot
