.PHONY: clean build build_android build_windows test

SRCS := src/patch_abl.c \
	src/patchs/core.c \
	src/patchs/libavb_force_success.c \
	src/patchs/pe_sections.c \
	src/patchs/oplus/warning.c \
	src/patchs/oplus/forceenablefastboot.c \
	src/arm64_inst/arm64_inst_decoder.c \
	src/arm64_inst/utils.c
CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -Iinclude

clean:
	rm -rf build || true

build:
	mkdir -p build
	gcc $(CFLAGS) -o build/patch_abl $(SRCS)

build_android:
	mkdir -p build
	$(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang $(CFLAGS) -o build/patch_abl_android $(SRCS)

test: tests/test_libavb_force_success.c src/patch_abl.c
	mkdir -p build
	$(CC) $(CFLAGS) -o build/test_libavb_force_success \
		tests/test_libavb_force_success.c \
		src/patchs/core.c \
		src/patchs/libavb_force_success.c \
		src/patchs/pe_sections.c \
		src/patchs/oplus/warning.c \
		src/patchs/oplus/forceenablefastboot.c \
		src/arm64_inst/arm64_inst_decoder.c \
		src/arm64_inst/utils.c
	./build/test_libavb_force_success

build_windows:
	mkdir -p build
	x86_64-w64-mingw32-gcc $(CFLAGS) -o build/patch_abl.exe $(SRCS)
