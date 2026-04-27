.PHONY: clean patch build dist build_superfbonly build_minimal_generic build_fastboot_boot_efi fastboot_boot_step0 build_keymaster_set_efi fastboot_boot_keymaster_set build_generic build_hooks build_hooks_generic patch_hooks fastboot_boot_hooks fastboot_boot_hooks_generic build_patcher_android build_patcher_android_keymaster build_module magisk_module_keymaster_set test_exploit test_boot test

clean:
	rm -rf edk2/Build || true
	rm -rf edk2/Conf || true
	rm edk2/QcomModulePkg/Include/Library/ABL.h || true
	rm tools/patch_abl || true
	rm -rf dist || true
	mkdir dist
patch: clean
	gcc -O2 -o ./tools/extractfv ./tools/extractfv.c -llzma
	./tools/extractfv ./images/abl.img -o ./dist
	rm ./tools/extractfv
	mv ./dist/LinuxLoader.efi ./dist/ABL_original.efi
	gcc -o tools/patch_abl tools/patch_abl.c
	./tools/patch_abl ./dist/ABL_original.efi ./dist/ABL.efi > ./dist/patch_log.txt
	rm tools/patch_abl
	cat ./dist/patch_log.txt
build: patch
	xxd -i dist/ABL.efi > edk2/QcomModulePkg/Include/Library/ABL.h
	cp -r ./Conf ./edk2/
	bash -c 'cd edk2 && . ./edksetup.sh && make BOARD_BOOTLOADER_PRODUCT_NAME=canoe TARGET_ARCHITECTURE=AARCH64 TARGET=RELEASE \
  		CLANG_BIN=/usr/bin/ CLANG_PREFIX=aarch64-linux-gnu- VERIFIED_BOOT_ENABLED=1 \
  		VERIFIED_BOOT_LE=0 AB_RETRYCOUNT_DISABLE=0 TARGET_BOARD_TYPE_AUTO=0 \
  		BUILD_USES_RECOVERY_AS_BOOT=0 DISABLE_PARALLEL_DOWNLOAD_FLASH=0 PVMFW_BCC_ENABLED=-DPVMFW_BCC\
  		REMOVE_CARVEOUT_REGION=1 QSPA_BOOTCONFIG_ENABLE=1 USER_BUILD_VARIANT=0 \
  		PREBUILT_HOST_TOOLS="BUILD_CC=clang BUILD_CXX=clang++ LDPATH=-fuse-ld=lld BUILD_AR=llvm-ar"' || true
	# test if the build is successful by checking the output file
	if [ ! -f edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ]; then \
		echo "Build failed"; \
		exit 1; \
	fi
	cp edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ./dist/ABL_with_superfastboot.efi
	cat ./dist/patch_log.txt
	ls -l ./dist
dist: build
	mkdir release
	zip -r release/$(DIST_NAME).zip dist

build_superfbonly: clean
	cp -r ./Conf ./edk2/
	bash -c 'cd edk2 && . ./edksetup.sh && make BOARD_BOOTLOADER_PRODUCT_NAME=canoe TARGET_ARCHITECTURE=AARCH64 TARGET=RELEASE \
  		CLANG_BIN=/usr/bin/ CLANG_PREFIX=aarch64-linux-gnu- VERIFIED_BOOT_ENABLED=1 \
  		VERIFIED_BOOT_LE=0 AB_RETRYCOUNT_DISABLE=0 TARGET_BOARD_TYPE_AUTO=0 \
  		BUILD_USES_RECOVERY_AS_BOOT=0 DISABLE_PARALLEL_DOWNLOAD_FLASH=0 PVMFW_BCC_ENABLED=-DPVMFW_BCC\
  		REMOVE_CARVEOUT_REGION=1 QSPA_BOOTCONFIG_ENABLE=1 USER_BUILD_VARIANT=0 TEST_ADAPTER=1 DISABLE_PRINT=1 DISABLE_PRINT_2=0\
  		PREBUILT_HOST_TOOLS="BUILD_CC=clang BUILD_CXX=clang++ LDPATH=-fuse-ld=lld BUILD_AR=llvm-ar"' || true
	# test if the build is successful by checking the output file
	if [ ! -f edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ]; then \
		echo "Build failed"; \
		exit 1; \
	fi
	cp edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ./dist/superfastboot.efi
	ls -l ./dist

build_minimal_generic: clean
	cp -r ./Conf ./edk2/
	bash -c 'cd edk2 && . ./edksetup.sh && make BOARD_BOOTLOADER_PRODUCT_NAME=canoe TARGET_ARCHITECTURE=AARCH64 TARGET=RELEASE \
  		CLANG_BIN=/usr/bin/ CLANG_PREFIX=aarch64-linux-gnu- VERIFIED_BOOT_ENABLED=1 \
  		VERIFIED_BOOT_LE=0 AB_RETRYCOUNT_DISABLE=0 TARGET_BOARD_TYPE_AUTO=0 \
  		BUILD_USES_RECOVERY_AS_BOOT=0 DISABLE_PARALLEL_DOWNLOAD_FLASH=0 PVMFW_BCC_ENABLED=-DPVMFW_BCC\
  		REMOVE_CARVEOUT_REGION=1 QSPA_BOOTCONFIG_ENABLE=1 USER_BUILD_VARIANT=0 AUTO_PATCH_ABL=1 \
  		MINIMAL_PATCH=1 ENABLE_KEYMASTER_HOOKS=1 DISABLE_PRINT=0 DISABLE_PRINT_2=0 \
  		PREBUILT_HOST_TOOLS="BUILD_CC=clang BUILD_CXX=clang++ LDPATH=-fuse-ld=lld BUILD_AR=llvm-ar"' || true
	# test if the build is successful by checking the output file
	if [ ! -f edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ]; then \
		echo "Build failed"; \
		exit 1; \
	fi
	cp edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ./dist/minimal_generic_superfastboot.efi
	ls -l ./dist

# Step 0: build a fastboot-bootable GBL EFI that keeps the normal boot path.
# AUTO_PATCH_ABL remains enabled, but AVB/cmdline/bootstate-altering patches are
# disabled; only EFISP recursion prevention is left for the chained ABL.
build_fastboot_boot_efi: build_minimal_generic
	@echo "Step-0 EFI ready: dist/minimal_generic_superfastboot.efi"
	@echo "Boot with: fastboot boot dist/minimal_generic_superfastboot.efi"

fastboot_boot_step0: build_fastboot_boot_efi
	fastboot boot ./dist/minimal_generic_superfastboot.efi

# Hook-only path. Per-stage AVB/cmdline/RPMB/lockstate flips (patches 2-9)
# are replaced by the QSEECOM + SCM protocol hooks defined in
# tools/qseecom_hook.h and tools/scm_hook.h. ENABLE_KEYMASTER_HOOKS=1 in the
# DSC implies -DDISABLE_PATCH_2..9 so only patch 1 (EFISP recursion fix) runs
# at the in-binary level.

# build_hooks: static path. Extract OEM ABL from images/abl.img, host-patch
# patch 1 only, bake into ABL.h via xxd, compile GBL Loader with hooks. The
# baked payload is what gets chain-loaded at runtime; the hooks intercept
# QSEECOM + SCM in flight.
patch_hooks: clean
	gcc -O2 -o ./tools/extractfv ./tools/extractfv.c -llzma
	./tools/extractfv ./images/abl.img -o ./dist
	rm ./tools/extractfv
	mv ./dist/LinuxLoader.efi ./dist/ABL_original.efi
	gcc -DDISABLE_PATCH_2 -DDISABLE_PATCH_3 -DDISABLE_PATCH_4 -DDISABLE_PATCH_5 -DDISABLE_PATCH_6 -DDISABLE_PATCH_7 -DDISABLE_PATCH_8 -DDISABLE_PATCH_9 -o tools/patch_abl tools/patch_abl.c
	./tools/patch_abl ./dist/ABL_original.efi ./dist/ABL.efi > ./dist/patch_log.txt
	rm tools/patch_abl
	cat ./dist/patch_log.txt

build_hooks: patch_hooks
	xxd -i dist/ABL.efi > edk2/QcomModulePkg/Include/Library/ABL.h
	cp -r ./Conf ./edk2/
	bash -c 'cd edk2 && . ./edksetup.sh && make BOARD_BOOTLOADER_PRODUCT_NAME=canoe TARGET_ARCHITECTURE=AARCH64 TARGET=RELEASE \
  		CLANG_BIN=/usr/bin/ CLANG_PREFIX=aarch64-linux-gnu- VERIFIED_BOOT_ENABLED=1 \
  		VERIFIED_BOOT_LE=0 AB_RETRYCOUNT_DISABLE=0 TARGET_BOARD_TYPE_AUTO=0 \
  		BUILD_USES_RECOVERY_AS_BOOT=0 DISABLE_PARALLEL_DOWNLOAD_FLASH=0 PVMFW_BCC_ENABLED=-DPVMFW_BCC\
  		REMOVE_CARVEOUT_REGION=1 QSPA_BOOTCONFIG_ENABLE=1 USER_BUILD_VARIANT=0 \
  		ENABLE_KEYMASTER_HOOKS=1 DISABLE_PRINT=1 DISABLE_PRINT_2=1 \
  		PREBUILT_HOST_TOOLS="BUILD_CC=clang BUILD_CXX=clang++ LDPATH=-fuse-ld=lld BUILD_AR=llvm-ar"' || true
	if [ ! -f edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ]; then \
		echo "Build failed"; \
		exit 1; \
	fi
	cp edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ./dist/hooks_static.efi
	ls -l ./dist
	@echo "Hooks (static) EFI ready: dist/hooks_static.efi"
	@echo "Boot with: fastboot boot dist/hooks_static.efi"

# build_hooks_generic: runtime path. AUTO_PATCH_ABL=1 reads ABL from the
# partition at boot, applies patch 1 in memory, installs hooks, chain-loads.
# No images/abl.img needed.
build_hooks_generic: clean
	cp -r ./Conf ./edk2/
	bash -c 'cd edk2 && . ./edksetup.sh && make BOARD_BOOTLOADER_PRODUCT_NAME=canoe TARGET_ARCHITECTURE=AARCH64 TARGET=RELEASE \
  		CLANG_BIN=/usr/bin/ CLANG_PREFIX=aarch64-linux-gnu- VERIFIED_BOOT_ENABLED=1 \
  		VERIFIED_BOOT_LE=0 AB_RETRYCOUNT_DISABLE=0 TARGET_BOARD_TYPE_AUTO=0 \
  		BUILD_USES_RECOVERY_AS_BOOT=0 DISABLE_PARALLEL_DOWNLOAD_FLASH=0 PVMFW_BCC_ENABLED=-DPVMFW_BCC\
  		REMOVE_CARVEOUT_REGION=1 QSPA_BOOTCONFIG_ENABLE=1 USER_BUILD_VARIANT=0 AUTO_PATCH_ABL=1 \
  		ENABLE_KEYMASTER_HOOKS=1 DISABLE_PRINT=0 DISABLE_PRINT_2=0 \
  		PREBUILT_HOST_TOOLS="BUILD_CC=clang BUILD_CXX=clang++ LDPATH=-fuse-ld=lld BUILD_AR=llvm-ar"' || true
	if [ ! -f edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ]; then \
		echo "Build failed"; \
		exit 1; \
	fi
	cp edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ./dist/hooks_generic.efi
	ls -l ./dist
	@echo "Hooks (generic) EFI ready: dist/hooks_generic.efi"
	@echo "Boot with: fastboot boot dist/hooks_generic.efi"

fastboot_boot_hooks: build_hooks
	fastboot boot ./dist/hooks_static.efi

fastboot_boot_hooks_generic: build_hooks_generic
	fastboot boot ./dist/hooks_generic.efi

# Deprecated alias — points at build_hooks_generic. .re-notes/sessions/
# entries still reference the old name; keep it working until those are
# rewritten or archived.
build_keymaster_set_efi: build_hooks_generic
	cp ./dist/hooks_generic.efi ./dist/keymaster_set_superfastboot.efi
	@echo "(deprecated) keymaster_set_superfastboot.efi == hooks_generic.efi"

fastboot_boot_keymaster_set: build_keymaster_set_efi
	fastboot boot ./dist/keymaster_set_superfastboot.efi

build_generic: clean
	cp -r ./Conf ./edk2/
	bash -c 'cd edk2 && . ./edksetup.sh && make BOARD_BOOTLOADER_PRODUCT_NAME=canoe TARGET_ARCHITECTURE=AARCH64 TARGET=RELEASE \
  		CLANG_BIN=/usr/bin/ CLANG_PREFIX=aarch64-linux-gnu- VERIFIED_BOOT_ENABLED=1 \
  		VERIFIED_BOOT_LE=0 AB_RETRYCOUNT_DISABLE=0 TARGET_BOARD_TYPE_AUTO=0 \
  		BUILD_USES_RECOVERY_AS_BOOT=0 DISABLE_PARALLEL_DOWNLOAD_FLASH=0 PVMFW_BCC_ENABLED=-DPVMFW_BCC\
  		REMOVE_CARVEOUT_REGION=1 QSPA_BOOTCONFIG_ENABLE=1 USER_BUILD_VARIANT=0 AUTO_PATCH_ABL=1 DISABLE_PRINT=0 DISABLE_PRINT_2=0\
  		PREBUILT_HOST_TOOLS="BUILD_CC=clang BUILD_CXX=clang++ LDPATH=-fuse-ld=lld BUILD_AR=llvm-ar"' || true
	# test if the build is successful by checking the output file
	if [ ! -f edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ]; then \
		echo "Build failed"; \
		exit 1; \
	fi
	cp edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ./dist/generic_superfastboot.efi
	ls -l ./dist

build_patcher_android: clean
	$(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang tools/patch_abl.c -o dist/patch_abl_android
	bash ./tools/build_extractfv_android.sh
build_patcher_android_keymaster: clean
	$(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang -DENABLE_KEYMASTER_HOOKS -DDISABLE_PATCH_2 -DDISABLE_PATCH_3 -DDISABLE_PATCH_4 -DDISABLE_PATCH_5 -DDISABLE_PATCH_6 -DDISABLE_PATCH_7 -DDISABLE_PATCH_8 -DDISABLE_PATCH_9 tools/patch_abl.c -o dist/patch_abl_android
	bash ./tools/build_extractfv_android.sh
build_module: build_patcher_android
	mv dist/patch_abl_android magisk_module/bin/patch_abl
	mv dist/extractfv_android magisk_module/bin/extractfv
	mkdir release || true
	cd magisk_module && zip -r ../release/$(DIST_NAME).zip ./
	rm magisk_module/bin/patch_abl
	rm magisk_module/bin/extractfv
magisk_module_keymaster_set: build_patcher_android_keymaster
	mv dist/patch_abl_android magisk_module/bin/patch_abl
	mv dist/extractfv_android magisk_module/bin/extractfv
	mkdir release || true
	cd magisk_module && zip -r ../release/$(DIST_NAME)-keymaster-set.zip ./
	rm magisk_module/bin/patch_abl
	rm magisk_module/bin/extractfv

test_exploit:
	@echo "This script is used to test the ABL exploit. Please make sure you tested before ota."
	@echo Please enter the Builtin Fastboot in the project. And put abl.img in the images folder. Press Enter to continue.
	@bash -c read -n 1 -s
	@python tools/extractfv.py ./images/abl.img ./ABL_original.efi
	@fastboot boot ./ABL_original.efi
	@echo 'If the exploit existed in the new abl image, the device will show two lines of "Press Volume Down key to enter Fastboot mode, waiting for 5 seconds into Normal mode..."'
	@echo 'If the exploit does not exist in the new abl image, the device will show red state screen'
	@rm ./ABL_original.efi
test_boot: build
	fastboot boot ./dist/ABL_with_superfastboot.efi

test:
	bash ./tests/runall.sh
