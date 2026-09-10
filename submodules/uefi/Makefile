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
	python3 embed_variant.py blobs/canoe-managed-msd.efi \
		edk2/QcomModulePkg/Application/LinuxLoader/Generated/CanoeManagedMsdVariantData.c CanoeManagedMsd
	# This vendor EDK2 build has reused objects after included headers changed
	# (a 48 MiB image-disk limit survived the 256 MiB header update). A fresh
	# final EFI alone does not prove fresh code: rebuild the full BDS output tree.
	# Standalone AndroidTools have their own output tree and clean build target.
	rm -rf edk2/Build/RELEASE_CLANG35
	rm -f ./build/BDS.efi
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
	printf '%s\n' '$(CANOE_VERSION)' > edk2/Build/.canoe-version
	@echo "BDS built successfully: build/BDS.efi"

# Build the standalone AndroidToolsPkg applications (RebootTools, ArbTools,
# BLTools, SurfaceTools, UsbTools, LogTools, MdTools, CrashTools).
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
	   [ ! -f $$TOOLS_DIR/BLTools.efi ] || [ ! -f $$TOOLS_DIR/SurfaceTools.efi ] || \
	   [ ! -f $$TOOLS_DIR/UsbTools.efi ] || [ ! -f $$TOOLS_DIR/LogTools.efi ] || \
	   [ ! -f $$TOOLS_DIR/MdTools.efi ] || [ ! -f $$TOOLS_DIR/CrashTools.efi ]; then \
		echo "AndroidToolsPkg build failed"; \
		exit 1; \
	fi
	mkdir -p ./build
	cp edk2/Build/AndroidToolsPkg/RELEASE_CLANG35/AARCH64/RebootTools.efi ./build/RebootTools.efi
	cp edk2/Build/AndroidToolsPkg/RELEASE_CLANG35/AARCH64/ArbTools.efi ./build/ArbTools.efi
	cp edk2/Build/AndroidToolsPkg/RELEASE_CLANG35/AARCH64/BLTools.efi ./build/BLTools.efi
	cp edk2/Build/AndroidToolsPkg/RELEASE_CLANG35/AARCH64/SurfaceTools.efi ./build/SurfaceTools.efi
	cp edk2/Build/AndroidToolsPkg/RELEASE_CLANG35/AARCH64/UsbTools.efi ./build/UsbTools.efi
	cp edk2/Build/AndroidToolsPkg/RELEASE_CLANG35/AARCH64/LogTools.efi ./build/LogTools.efi
	cp edk2/Build/AndroidToolsPkg/RELEASE_CLANG35/AARCH64/MdTools.efi ./build/MdTools.efi
	cp edk2/Build/AndroidToolsPkg/RELEASE_CLANG35/AARCH64/CrashTools.efi ./build/CrashTools.efi
	@echo "AndroidTools built successfully: build/RebootTools.efi build/ArbTools.efi build/BLTools.efi build/SurfaceTools.efi build/UsbTools.efi build/LogTools.efi build/MdTools.efi build/CrashTools.efi"

tools-clean:
	bash -c 'cd edk2 && . ./edksetup.sh && make tools-clean TARGET_ARCHITECTURE=AARCH64 TARGET=RELEASE \
  		CLANG_BIN=/usr/bin/ CLANG_PREFIX=aarch64-linux-gnu- \
  		PREBUILT_HOST_TOOLS="BUILD_CC=clang BUILD_CXX=clang++ LDPATH=-fuse-ld=lld BUILD_AR=llvm-ar"' || true
	rm -f ./build/RebootTools.efi ./build/ArbTools.efi ./build/BLTools.efi \
		./build/SurfaceTools.efi ./build/LogTools.efi \
		./build/UsbTools.efi ./build/MdTools.efi ./build/CrashTools.efi || true

test:
	$(MAKE) -C tests test

