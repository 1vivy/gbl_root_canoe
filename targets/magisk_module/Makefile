CANOE_MANAGER_SRC ?= $(abspath ../../../canoe-boot-manager/native/canoe-manager)
CANOE_ROOT_DIR := $(abspath ../..)
include ../../version.mk
include ../../imports.mk

.PHONY: canoe_manager clean copy_resources webui_dist webui-pin submodule_uefi submodule_patcher \
	submodule_mode2_profile submodule_abl_tzmap submodule_canoe_bootmgr \
	submodule_ablfvextractor build

CANOE_KSU_DIST ?= $(abspath ../../../canoe-boot-manager/dist/ksu)

NDK_PATH ?= $(ANDROID_NDK_LATEST_HOME)
export NDK_PATH

# Shipped modules are aarch64 (the only architecture the devices have). The
# app's e2e harness builds a test-only x86_64 module for an x86_64 Cuttlefish
# guest with `make target_magisk_module MODULE_ARCH=x86_64`. All native helpers
# use that architecture; the ABL/UEFI images they process remain ARM firmware.
MODULE_ARCH ?= aarch64
export ANDROID_ARCH := $(MODULE_ARCH)
ANDROID_TARGET := $(MODULE_ARCH)-linux-android
ANDROID_TARGET_ENV := $(subst -,_,$(ANDROID_TARGET))
ANDROID_CLANG := $(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64/bin/$(ANDROID_TARGET)31-clang
ANDROID_AR := $(ANDROID_AR)
ANDROID_LINKER_VAR := CARGO_TARGET_$(shell printf '%s' '$(ANDROID_TARGET_ENV)' | tr a-z A-Z)_LINKER

clean:
	rm -rf build || true
copy_resources:
	rm -rf build/module build/module_android.zip
	mkdir -p build/module
	mkdir -p build/module/bin
	cp -r module/* build/module/
	[ -d ../../ablrepo ] && cp -r ../../ablrepo ./build/module/ablrepo || true
# b5 packages only the explicit local KSU build. Never fetch the b4 archive.
webui_dist: copy_resources
	python3 ../../scripts/stage_ksu.py "$(CANOE_KSU_DIST)" build/module/webroot "$(CANOE_VERSION)"
webui-pin:
	@echo 'b5 uses CANOE_KSU_DIST; build the app KSU target first.' >&2
	@exit 2
submodule_uefi: copy_resources
	# Build the EDK2 tree at most once per workspace, and at most once per
	# invocation. `make build` in submodules/uefi relinks LinuxLoader with
	# different bytes every time, so rebuilding once per package would ship a
	# different BDS.efi in each archive and break the CI check that every package
	# carries byte-identical boot artifacts. The guard is therefore a plain
	# existence test. To pick up edited UEFI sources, run
	# `UEFI_REBUILD=1 make target_<name>` from the REPO ROOT, whose uefi_discard
	# target drops these artifacts once for the whole invocation.
	# BDS and the standalone tools are guarded SEPARATELY. Rebuilding the tools
	# must not drag in a second `make build`, because that relinks LinuxLoader
	# with different bytes and breaks the byte-identical BDS.efi invariant.
	cd ../../submodules/uefi && \
		if [ ! -f build/BDS.efi ]; then make build; fi && \
		if [ ! -f build/ArbTools.efi ] || [ ! -f build/BLTools.efi ] || \
		   [ ! -f build/RebootTools.efi ] || [ ! -f build/SurfaceTools.efi ] || \
		   [ ! -f build/UsbTools.efi ]; then make tools; fi
	cp ../../submodules/uefi/build/BDS.efi ./build/module/BDS.efi
	mkdir -p build/module/efisp/tools
	cp ../../submodules/uefi/build/ArbTools.efi ./build/module/efisp/tools/ArbTools.efi
	cp ../../submodules/uefi/build/BLTools.efi ./build/module/efisp/tools/BLTools.efi
	cp ../../submodules/uefi/build/RebootTools.efi ./build/module/efisp/tools/RebootTools.efi
	cp ../../submodules/uefi/build/SurfaceTools.efi ./build/module/efisp/tools/SurfaceTools.efi
	cp ../../submodules/uefi/build/UsbTools.efi ./build/module/efisp/tools/UsbTools.efi
submodule_patcher: copy_resources
	cd ../../submodules/patcher && make build_android
	cp ../../submodules/patcher/build/patch_abl_android ./build/module/bin/patch_abl
submodule_mode2_profile: copy_resources
	test -n "$(NDK_PATH)" || { echo "NDK_PATH is required for mode2_profile"; exit 1; }
	test -x "$(ANDROID_CLANG)" || { echo "Android NDK clang is unavailable"; exit 1; }
	test -d "$$(rustc --print target-libdir --target $(ANDROID_TARGET) 2>/dev/null)" || { echo "Rust Android std is unavailable; use rustup cargo first on PATH (for example: PATH=\"\$$HOME/.cargo/bin:\$$PATH\" make target_magisk_module NDK_PATH=/path/to/android-ndk) with $(ANDROID_TARGET) installed"; exit 1; }
	$(ANDROID_LINKER_VAR)="$(ANDROID_CLANG)" cargo build --locked --release --target $(ANDROID_TARGET) --manifest-path ../../tools/mode2-profile/Cargo.toml
	test -f ../../tools/mode2-profile/target/$(ANDROID_TARGET)/release/mode2_profile
	cp ../../tools/mode2-profile/target/$(ANDROID_TARGET)/release/mode2_profile ./build/module/bin/mode2_profile
submodule_abl_tzmap: copy_resources
	test -n "$(NDK_PATH)" || { echo "NDK_PATH is required for abl_tzmap"; exit 1; }
	test -x "$(ANDROID_CLANG)" || { echo "Android NDK clang is unavailable"; exit 1; }
	$(ANDROID_LINKER_VAR)="$(ANDROID_CLANG)" cargo build --locked --release --target $(ANDROID_TARGET) --manifest-path ../../tools/abl-tzmap/Cargo.toml
	test -f ../../tools/abl-tzmap/target/$(ANDROID_TARGET)/release/abl_tzmap
	cp ../../tools/abl-tzmap/target/$(ANDROID_TARGET)/release/abl_tzmap ./build/module/bin/abl_tzmap
submodule_canoe_bootmgr: copy_resources
	test -n "$(NDK_PATH)" || { echo "NDK_PATH is required for canoe-bootmgr"; exit 1; }
	test -x "$(ANDROID_CLANG)" || { echo "Android NDK clang is unavailable"; exit 1; }
	CC_$(ANDROID_TARGET_ENV)="$(ANDROID_CLANG)" \
	AR_$(ANDROID_TARGET_ENV)="$(ANDROID_AR)" \
	$(ANDROID_LINKER_VAR)="$(ANDROID_CLANG)" cargo build --locked --release --target $(ANDROID_TARGET) --manifest-path ../../tools/canoe-bootmgr/Cargo.toml
	test -f ../../tools/canoe-bootmgr/target/$(ANDROID_TARGET)/release/canoe-bootmgr
	cp ../../tools/canoe-bootmgr/target/$(ANDROID_TARGET)/release/canoe-bootmgr ./build/module/bin/canoe-bootmgr
submodule_canoe_image: copy_resources
	test -n "$(NDK_PATH)" || { echo "NDK_PATH is required for canoe-image"; exit 1; }
	test -x "$(ANDROID_CLANG)" || { echo "Android NDK clang is unavailable"; exit 1; }
	CC_$(ANDROID_TARGET_ENV)="$(ANDROID_CLANG)" \
	AR_$(ANDROID_TARGET_ENV)="$(ANDROID_AR)" \
	$(ANDROID_LINKER_VAR)="$(ANDROID_CLANG)" cargo build --locked --release --target $(ANDROID_TARGET) --manifest-path ../../tools/canoe-image/Cargo.toml
	test -f ../../tools/canoe-image/target/$(ANDROID_TARGET)/release/canoe-image
	cp ../../tools/canoe-image/target/$(ANDROID_TARGET)/release/canoe-image ./build/module/bin/canoe-image
submodule_canoe_provision: copy_resources
	test -n "$(NDK_PATH)" || { echo "NDK_PATH is required for canoe-provision"; exit 1; }
	test -x "$(ANDROID_CLANG)" || { echo "Android NDK clang is unavailable"; exit 1; }
	CC_$(ANDROID_TARGET_ENV)="$(ANDROID_CLANG)" \
	AR_$(ANDROID_TARGET_ENV)="$(ANDROID_AR)" \
	$(ANDROID_LINKER_VAR)="$(ANDROID_CLANG)" cargo build --locked --release --target $(ANDROID_TARGET) --manifest-path ../../tools/canoe-provision/Cargo.toml
	test -f ../../tools/canoe-provision/target/$(ANDROID_TARGET)/release/canoe-provision
	cp ../../tools/canoe-provision/target/$(ANDROID_TARGET)/release/canoe-provision ./build/module/bin/canoe-provision
canoe_manager: copy_resources
	test -n "$(NDK_PATH)" || { echo "NDK_PATH is required for canoe-manager"; exit 1; }
	test -x "$(ANDROID_CLANG)" || { echo "Android NDK clang is unavailable"; exit 1; }
	CC_$(ANDROID_TARGET_ENV)="$(ANDROID_CLANG)" \
	AR_$(ANDROID_TARGET_ENV)="$(ANDROID_AR)" \
	$(ANDROID_LINKER_VAR)="$(ANDROID_CLANG)" cargo build --locked --release --target $(ANDROID_TARGET) --manifest-path "$(CANOE_MANAGER_SRC)/Cargo.toml"
	test -f "$(CANOE_MANAGER_SRC)/target/$(ANDROID_TARGET)/release/canoe-manager"
	cp "$(CANOE_MANAGER_SRC)/target/$(ANDROID_TARGET)/release/canoe-manager" ./build/module/bin/canoe-manager
submodule_ablfvextractor: copy_resources
	cd ../../submodules/ablfvextractor && make build_android
	cp ../../submodules/ablfvextractor/build/extractfv_android ./build/module/bin/extractfv
build: webui_dist submodule_uefi submodule_patcher submodule_mode2_profile submodule_abl_tzmap submodule_canoe_bootmgr submodule_canoe_image submodule_canoe_provision canoe_manager submodule_ablfvextractor
	# zip
	cd build/module && zip -r ../module_android.zip ./
