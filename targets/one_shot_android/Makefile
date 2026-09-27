# Android one-shot install package.
#
# Firmware CI builds this archive from the same EDK2 output it packages, and
# scripts/firmware_release.py attaches it to the same draft release as BDS.efi
# after checking that its embedded firmware is byte-identical to the staged
# EFI files. The archive therefore cannot carry a different firmware build than
# the one being qualified.
#
# It is not a general toolbox. It holds the rooted-shell installer, the four
# commands that installer drives, the released BDS and the reviewed EFI tool
# set the installers stage into the boot root.
NDK_PATH ?= $(or $(ANDROID_NDK_LATEST_HOME),$(ANDROID_NDK_HOME),$(ANDROID_NDK_ROOT))
include ../../version.mk

ANDROID_TARGET := aarch64-linux-android
ANDROID_API := 31
ANDROID_TOOLCHAIN := $(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64/bin
ANDROID_CLANG := $(ANDROID_TOOLCHAIN)/$(ANDROID_TARGET)$(ANDROID_API)-clang
ANDROID_AR := $(ANDROID_TOOLCHAIN)/llvm-ar
ANDROID_LINKER_VAR := CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER

# canoe-image resolves mode2_profile from its --tools directory, so that helper
# ships beside the commands the installer names.
COMMANDS := canoe-image canoe-provision canoe-bootmgr mode2_profile
# Exactly the tools the other installers stage into the boot root, so the boot
# root's tool directory does not depend on which installer put it there.
STAGED_TOOLS := ArbTools.efi BLTools.efi RebootTools.efi SurfaceTools.efi UsbTools.efi

PACKAGE := build/canoe-one-shot-$(CANOE_VERSION)-android-arm64.zip
STAGE := build/stage
BUILD_DIR := ../../submodules/uefi/build

.PHONY: clean guard build

clean:
	rm -rf build

guard:
	@set -eu; \
	test -n "$(NDK_PATH)" || { echo "NDK_PATH, ANDROID_NDK_LATEST_HOME, ANDROID_NDK_HOME, or ANDROID_NDK_ROOT is required"; exit 1; }; \
	test -x "$(ANDROID_CLANG)" || { echo "Android NDK clang is unavailable: $(ANDROID_CLANG)"; exit 1; }; \
	test -d "$$(rustc --print target-libdir --target $(ANDROID_TARGET) 2>/dev/null)" || { \
		echo "Rust Android std is unavailable; install $(ANDROID_TARGET) with rustup"; exit 1; }; \
	test -f $(BUILD_DIR)/BDS.efi || { echo "the canonical EDK2 build has not produced $(BUILD_DIR)/BDS.efi"; exit 1; }; \
	for tool in $(STAGED_TOOLS); do \
		test -f $(BUILD_DIR)/$$tool || { echo "the canonical EDK2 build is missing $(BUILD_DIR)/$$tool"; exit 1; }; \
	done

# Build one Android command and stage the binary the installer invokes by name.
# $(1) is the command name, $(2) the crate directory that produces it.
define android_command
	CC_aarch64_linux_android="$(ANDROID_CLANG)" \
	AR_aarch64_linux_android="$(ANDROID_AR)" \
	$(ANDROID_LINKER_VAR)="$(ANDROID_CLANG)" cargo build --locked --release \
		--target $(ANDROID_TARGET) \
		--manifest-path ../../tools/$(2)/Cargo.toml
	cp ../../tools/$(2)/target/$(ANDROID_TARGET)/release/$(1) $(STAGE)/bin/$(1)
endef

build: guard
	rm -rf build
	mkdir -p $(STAGE)/bin $(STAGE)/efisp/tools
	cp resources/install-canoe.sh resources/README.txt $(STAGE)/
	cp $(BUILD_DIR)/BDS.efi $(STAGE)/BDS.efi
	for tool in $(STAGED_TOOLS); do cp $(BUILD_DIR)/$$tool $(STAGE)/efisp/tools/$$tool; done
	$(call android_command,canoe-image,canoe-image)
	$(call android_command,canoe-provision,canoe-provision)
	$(call android_command,canoe-bootmgr,canoe-bootmgr)
	$(call android_command,mode2_profile,mode2-profile)
	# zip stores the Unix modes, and Android's unzip does not always restore
	# them, so the documented unpack step chmods again; set them here anyway.
	chmod 755 $(STAGE)/install-canoe.sh $(STAGE)/bin/*
	chmod 644 $(STAGE)/README.txt $(STAGE)/BDS.efi $(STAGE)/efisp/tools/*
	# One mtime for every member keeps the archive reproducible.
	epoch=$$(git -C ../.. show -s --format=%ct HEAD); \
	find $(STAGE) -exec touch -d "@$$epoch" {} +
	rm -f $(PACKAGE)
	cd $(STAGE) && zip -X -q -9 -r ../../$(PACKAGE) BDS.efi README.txt install-canoe.sh bin efisp
	# scripts/firmware_release.py owns the release contract for this archive;
	# the listing here is the build's visible evidence.
	unzip -Z1 $(PACKAGE)
	sha256sum $(PACKAGE)
