.PHONY: clean build build_android build_windows test

SRCS := src/patch_abl.c \
	src/patchs/core.c \
	src/patchs/libavb_force_success.c \
	src/patchs/pe_sections.c
CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -Iinclude

clean:
	rm -rf build || true

build:
	mkdir -p build
	gcc $(CFLAGS) -o build/patch_abl $(SRCS)

build_android:
	mkdir -p build
	$(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang $(CFLAGS) -o build/patch_abl_android $(SRCS)

test: tests/test_patch10.c src/patch_abl.c
	mkdir -p build
	$(CC) $(CFLAGS) -o build/test_patch10 \
		tests/test_patch10.c \
		src/patchs/core.c \
		src/patchs/libavb_force_success.c \
		src/patchs/pe_sections.c
	./build/test_patch10

build_windows:
	mkdir -p build
	x86_64-w64-mingw32-gcc $(CFLAGS) -o build/patch_abl.exe $(SRCS)
