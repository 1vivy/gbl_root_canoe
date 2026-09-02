CANOE_ROOT_DIR := $(abspath ../..)
include ../../version.mk

.PHONY: clean copy_resources webui_dist webui-pin submodule_uefi submodule_patcher \
	submodule_mode2_profile submodule_abl_tzmap submodule_canoe_bootmgr \
	submodule_ablfvextractor build

CANOE_WEBUI_ARCHIVE := $(CANOE_ROOT_DIR)/.cache/canoe-boot-manager/canoe-boot-manager-$(CANOE_WEBUI_VERSION).tar.gz

NDK_PATH ?= $(ANDROID_NDK_LATEST_HOME)
export NDK_PATH

clean:
	rm -rf build || true
copy_resources:
	rm -rf build/module build/module_android.zip
	mkdir -p build/module
	cp -r module/* build/module/
	[ -d ../../ablrepo ] && cp -r ../../ablrepo ./build/module/ablrepo || true
# Fetch the pinned static app into the root cache, then replace webroot
# wholesale. The source archive is a checked-in last-known-good artifact
# until the canoe-boot-manager repository publishes release assets.
webui_dist: copy_resources
	$(MAKE) -C "$(CANOE_ROOT_DIR)" fetch-verified \
		FETCH_URL="$(CANOE_WEBUI_URL)" \
		FETCH_SHA256="$(CANOE_WEBUI_SHA256)" \
		FETCH_DEST="$(CANOE_WEBUI_ARCHIVE)"
	@set -eu; \
	tmp="$$(mktemp -d "$(CURDIR)/build/.webroot.XXXXXX")"; \
	trap 'rm -rf "$$tmp"' EXIT HUP INT TERM; \
	tar --extract --gzip --file "$(CANOE_WEBUI_ARCHIVE)" \
		--directory "$$tmp" --no-same-owner; \
	test -f "$$tmp/index.html"; \
	rm -rf build/module/webroot; \
	mv "$$tmp" build/module/webroot; \
	trap - EXIT HUP INT TERM; \
	rm -rf "$$tmp"
# Refresh the checked-in last-known-good archive from a local checkout of the
# app, and print the digest to paste into version.mk. The tar flags are what
# make the digest stable: without them every regeneration embeds a new mtime
# and owner, and a pin that changes on every run pins nothing.
#
#   make -C targets/magisk_module webui-pin CANOE_WEBUI_SRC=/path/to/canoe-boot-manager
CANOE_WEBUI_SRC ?= ../../../canoe-boot-manager
webui-pin:
	@set -eu; \
	test -f "$(CANOE_WEBUI_SRC)/dist/index.html" || \
		{ echo "run 'bun run build' in $(CANOE_WEBUI_SRC) first" >&2; exit 1; }; \
	out="webui-cache/canoe-boot-manager-$(CANOE_WEBUI_VERSION).tar.gz"; \
	tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
		--format=gnu -C "$(CANOE_WEBUI_SRC)/dist" -cf - . \
		| gzip -n -9 > "$$out"; \
	echo "CANOE_WEBUI_SHA256 = $$(sha256sum "$$out" | cut -d' ' -f1)"
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
	test -x "$(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang" || { echo "Android NDK clang is unavailable"; exit 1; }
	test -d "$$(rustc --print target-libdir --target aarch64-linux-android 2>/dev/null)" || { echo "Rust Android std is unavailable; use a rustup toolchain with aarch64-linux-android installed and put its cargo first on PATH"; exit 1; }
	CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER="$(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang" cargo build --locked --release --target aarch64-linux-android --manifest-path ../../tools/mode2-profile/Cargo.toml
	test -f ../../tools/mode2-profile/target/aarch64-linux-android/release/mode2_profile
	cp ../../tools/mode2-profile/target/aarch64-linux-android/release/mode2_profile ./build/module/bin/mode2_profile
submodule_abl_tzmap: copy_resources
	test -n "$(NDK_PATH)" || { echo "NDK_PATH is required for abl_tzmap"; exit 1; }
	test -x "$(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang" || { echo "Android NDK clang is unavailable"; exit 1; }
	CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER="$(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang" cargo build --locked --release --target aarch64-linux-android --manifest-path ../../tools/abl-tzmap/Cargo.toml
	test -f ../../tools/abl-tzmap/target/aarch64-linux-android/release/abl_tzmap
	cp ../../tools/abl-tzmap/target/aarch64-linux-android/release/abl_tzmap ./build/module/bin/abl_tzmap
submodule_canoe_bootmgr: copy_resources
	test -n "$(NDK_PATH)" || { echo "NDK_PATH is required for canoe-bootmgr"; exit 1; }
	test -x "$(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang" || { echo "Android NDK clang is unavailable"; exit 1; }
	CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER="$(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang" cargo build --locked --release --target aarch64-linux-android --manifest-path ../../tools/canoe-bootmgr/Cargo.toml
	test -f ../../tools/canoe-bootmgr/target/aarch64-linux-android/release/canoe-bootmgr
	cp ../../tools/canoe-bootmgr/target/aarch64-linux-android/release/canoe-bootmgr ./build/module/bin/canoe-bootmgr
submodule_ablfvextractor: copy_resources
	cd ../../submodules/ablfvextractor && make build_android
	cp ../../submodules/ablfvextractor/build/extractfv_android ./build/module/bin/extractfv
build: webui_dist submodule_uefi submodule_patcher submodule_mode2_profile submodule_abl_tzmap submodule_canoe_bootmgr submodule_ablfvextractor
	# zip
	cd build/module && zip -r ../module_android.zip ./
