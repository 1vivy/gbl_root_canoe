include ../../version.mk

.PHONY: clean build tools tools-clean test

clean:
	rm -rf edk2/Build || true
	rm -rf edk2/Conf || true
	rm -rf ./build || true
build:
	cp -r ./Conf ./edk2/
	# Bake the bundled mass-storage driver into the image; a missing blob
	# builds the platform-driver fallback instead.
	python3 embed_variant.py blobs/canoe-usbmsd.efi \
		edk2/QcomModulePkg/Application/LinuxLoader/Generated/CanoeMsdVariantData.c
	# Remove the previous output first. The EDK2 invocation below is tolerated
	# with `|| true`, so an existence check alone would accept a FAILED rebuild
	# whenever a stale artifact was still lying around, and the packages would
	# then ship it silently.
	rm -f edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi
	bash -c 'cd edk2 && . ./edksetup.sh && make BOARD_BOOTLOADER_PRODUCT_NAME=canoe CANOE_VERSION="$(CANOE_VERSION)" TARGET_ARCHITECTURE=AARCH64 TARGET=RELEASE \
  		CLANG_BIN=/usr/bin/ CLANG_PREFIX=aarch64-linux-gnu- VERIFIED_BOOT_ENABLED=1 \
  		VERIFIED_BOOT_LE=0 AB_RETRYCOUNT_DISABLE=0 TARGET_BOARD_TYPE_AUTO=0 \
  		BUILD_USES_RECOVERY_AS_BOOT=0 DISABLE_PARALLEL_DOWNLOAD_FLASH=0 PVMFW_BCC_ENABLED=-DPVMFW_BCC\
  		REMOVE_CARVEOUT_REGION=1 QSPA_BOOTCONFIG_ENABLE=1 USER_BUILD_VARIANT=0 TEST_ADAPTER=1 \
  		PREBUILT_HOST_TOOLS="BUILD_CC=clang BUILD_CXX=clang++ LDPATH=-fuse-ld=lld BUILD_AR=llvm-ar"' || true
	# test if the build is successful by checking the output file
	if [ ! -f edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ]; then \
		echo "Build failed"; \
		exit 1; \
	fi
	mkdir -p ./build
	cp edk2/Build/RELEASE_CLANG35/AARCH64/LinuxLoader.efi ./build/BDS.efi
	@echo "BDS built successfully: build/BDS.efi"

# Build the standalone AndroidToolsPkg applications (RebootTools, ArbTools,
# BLTools, FdLoader, AbootLoader, SurfaceTools, UsbTools).
# These are independent UEFI apps launched from the boot menu or from a boot
# entry on removable media; they do not affect the main BDS image. Passes the
# same toolchain env as `build` so the CLANG35 toolchain and BaseTools resolve
# identically.
tools:
	cp -r ./Conf ./edk2/ 2>/dev/null || true
	# Remove all standalone-tool outputs so EDK2 cannot reuse a stale artifact.
	rm -rf edk2/Build/AndroidToolsPkg
	bash -c 'cd edk2 && . ./edksetup.sh && make tools BOARD_BOOTLOADER_PRODUCT_NAME=canoe CANOE_VERSION="$(CANOE_VERSION)" TARGET_ARCHITECTURE=AARCH64 TARGET=RELEASE \
  		CLANG_BIN=/usr/bin/ CLANG_PREFIX=aarch64-linux-gnu- VERIFIED_BOOT_ENABLED=1 \
  		VERIFIED_BOOT_LE=0 AB_RETRYCOUNT_DISABLE=0 TARGET_BOARD_TYPE_AUTO=0 \
  		BUILD_USES_RECOVERY_AS_BOOT=0 DISABLE_PARALLEL_DOWNLOAD_FLASH=0 PVMFW_BCC_ENABLED=-DPVMFW_BCC \
  		REMOVE_CARVEOUT_REGION=1 QSPA_BOOTCONFIG_ENABLE=1 USER_BUILD_VARIANT=0 TEST_ADAPTER=1 \
  		PREBUILT_HOST_TOOLS="BUILD_CC=clang BUILD_CXX=clang++ LDPATH=-fuse-ld=lld BUILD_AR=llvm-ar"' || true
	@TOOLS_DIR=edk2/Build/AndroidToolsPkg/RELEASE_CLANG35/AARCH64; \
	if [ ! -f $$TOOLS_DIR/RebootTools.efi ] || [ ! -f $$TOOLS_DIR/ArbTools.efi ] || \
	   [ ! -f $$TOOLS_DIR/BLTools.efi ] || [ ! -f $$TOOLS_DIR/FdLoader.efi ] || \
	   [ ! -f $$TOOLS_DIR/AbootLoader.efi ] || [ ! -f $$TOOLS_DIR/SurfaceTools.efi ] || \
	   [ ! -f $$TOOLS_DIR/UsbTools.efi ]; then \
		echo "AndroidToolsPkg build failed"; \
		exit 1; \
	fi
	mkdir -p ./build
	cp edk2/Build/AndroidToolsPkg/RELEASE_CLANG35/AARCH64/RebootTools.efi ./build/RebootTools.efi
	cp edk2/Build/AndroidToolsPkg/RELEASE_CLANG35/AARCH64/ArbTools.efi ./build/ArbTools.efi
	cp edk2/Build/AndroidToolsPkg/RELEASE_CLANG35/AARCH64/BLTools.efi ./build/BLTools.efi
	cp edk2/Build/AndroidToolsPkg/RELEASE_CLANG35/AARCH64/FdLoader.efi ./build/FdLoader.efi
	cp edk2/Build/AndroidToolsPkg/RELEASE_CLANG35/AARCH64/AbootLoader.efi ./build/AbootLoader.efi
	cp edk2/Build/AndroidToolsPkg/RELEASE_CLANG35/AARCH64/SurfaceTools.efi ./build/SurfaceTools.efi
	cp edk2/Build/AndroidToolsPkg/RELEASE_CLANG35/AARCH64/UsbTools.efi ./build/UsbTools.efi
	@echo "AndroidTools built successfully: build/RebootTools.efi build/ArbTools.efi build/BLTools.efi build/FdLoader.efi build/AbootLoader.efi build/SurfaceTools.efi build/UsbTools.efi"

tools-clean:
	bash -c 'cd edk2 && . ./edksetup.sh && make tools-clean TARGET_ARCHITECTURE=AARCH64 TARGET=RELEASE \
  		CLANG_BIN=/usr/bin/ CLANG_PREFIX=aarch64-linux-gnu- \
  		PREBUILT_HOST_TOOLS="BUILD_CC=clang BUILD_CXX=clang++ LDPATH=-fuse-ld=lld BUILD_AR=llvm-ar"' || true
	rm -f ./build/RebootTools.efi ./build/ArbTools.efi ./build/BLTools.efi \
		./build/FdLoader.efi ./build/AbootLoader.efi ./build/SurfaceTools.efi \
		./build/UsbTools.efi || true

test:
	$(MAKE) -C tests test

