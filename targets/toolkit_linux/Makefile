CANOE_MANAGER_SRC ?= $(abspath ../../../canoe-boot-manager/native/canoe-manager)
include ../../version.mk

.PHONY: canoe_manager clean copy_resources submodule_uefi submodule_patcher \
	submodule_mode2_profile submodule_abl_tzmap submodule_canoe_bootmgr \
	submodule_canoe_ext4 submodule_ablfvextractor \
	submodule_vbmetafixer canoe_manager canoe_app build

ROOT_DIR := $(abspath ../..)
CANOE_APP_LINUX_BIN ?= $(ROOT_DIR)/../canoe-boot-manager/src-tauri/target/release/canoe-boot-manager


NDK_PATH ?= $(ANDROID_NDK_LATEST_HOME)
export NDK_PATH

clean:
	rm -rf build || true
copy_resources:
	rm -rf build/toolkit build/toolkit_linux.zip
	mkdir -p build/toolkit
	mkdir -p build/toolkit/bin
	cp -r resources/* build/toolkit/
submodule_uefi: copy_resources
	# Build the EDK2 tree at most once per workspace, and at most once per
	# invocation. `make build` in submodules/uefi relinks LinuxLoader with
	# different bytes every time, so rebuilding once per package would ship a
	# different BDS.efi in each archive and break the CI check that every
	# package carries byte-identical boot artifacts. The guard is therefore a
	# plain existence test: the first package to run does the single build and
	# the rest reuse it. To pick up edited UEFI sources, run
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
	cp ../../submodules/uefi/build/BDS.efi ./build/toolkit/BDS.efi
	mkdir -p build/toolkit/efisp/tools
	cp ../../submodules/uefi/build/ArbTools.efi ./build/toolkit/efisp/tools/ArbTools.efi
	cp ../../submodules/uefi/build/BLTools.efi ./build/toolkit/efisp/tools/BLTools.efi
	cp ../../submodules/uefi/build/RebootTools.efi ./build/toolkit/efisp/tools/RebootTools.efi
	cp ../../submodules/uefi/build/SurfaceTools.efi ./build/toolkit/efisp/tools/SurfaceTools.efi
	cp ../../submodules/uefi/build/UsbTools.efi ./build/toolkit/efisp/tools/UsbTools.efi
submodule_patcher: copy_resources
	cd ../../submodules/patcher && make build
	cp ../../submodules/patcher/build/patch_abl ./build/toolkit/bin/patch_abl
submodule_mode2_profile: copy_resources
	cargo build --locked --release --manifest-path ../../tools/mode2-profile/Cargo.toml
	test -x ../../tools/mode2-profile/target/release/mode2_profile
	cp ../../tools/mode2-profile/target/release/mode2_profile ./build/toolkit/bin/mode2_profile
submodule_abl_tzmap: copy_resources
	cargo build --locked --release --manifest-path ../../tools/abl-tzmap/Cargo.toml
	test -x ../../tools/abl-tzmap/target/release/abl_tzmap
	cp ../../tools/abl-tzmap/target/release/abl_tzmap ./build/toolkit/bin/abl_tzmap
submodule_canoe_bootmgr: copy_resources
	cargo build --locked --release --manifest-path ../../tools/canoe-bootmgr/Cargo.toml
	test -x ../../tools/canoe-bootmgr/target/release/canoe-bootmgr
	cp ../../tools/canoe-bootmgr/target/release/canoe-bootmgr ./build/toolkit/bin/canoe-bootmgr
canoe_manager: copy_resources
	cargo build --locked --release --manifest-path "$(CANOE_MANAGER_SRC)/Cargo.toml"
	test -x "$(CANOE_MANAGER_SRC)/target/release/canoe-manager"
	cp "$(CANOE_MANAGER_SRC)/target/release/canoe-manager" ./build/toolkit/bin/canoe-manager
submodule_canoe_ext4: copy_resources
	$(MAKE) -C ../../tools/canoe-ext4 static
	test -x ../../tools/canoe-ext4/canoe-ext4-static
	! readelf -d ../../tools/canoe-ext4/canoe-ext4-static | grep -q '(NEEDED)'
	cp ../../tools/canoe-ext4/canoe-ext4-static ./build/toolkit/bin/canoe-ext4
submodule_ablfvextractor: copy_resources
	cd ../../submodules/ablfvextractor && make build
	cp ../../submodules/ablfvextractor/build/extractfv ./build/toolkit/bin/extractfv
submodule_vbmetafixer: copy_resources
	# vbmetaport grafts an official vbmeta onto a third-party image;
	# vbmetabackup lifts that vbmeta out of a local firmware image.
	# Guarded like submodule_uefi: the vbmetafixer `build` target is not phony, so
	# a stale build/ directory would otherwise make a plain `make build` a no-op.
	cd ../../tools/vbmetafixer && \
		if [ ! -f build/toolkit/bin/vbmetaport ] || \
		   [ ! -f build/toolkit/bin/vbmetabackup ]; then $(MAKE) clean build; fi
	cp ../../tools/vbmetafixer/build/toolkit/bin/vbmetaport ./build/toolkit/bin/vbmetaport
	cp ../../tools/vbmetafixer/build/toolkit/bin/vbmetabackup ./build/toolkit/bin/vbmetabackup
canoe_app: copy_resources
	@test -x "$(CANOE_APP_LINUX_BIN)" || { \
		echo "canoe-boot-manager Linux binary is required; run 'cd ../canoe-boot-manager && bunx tauri build --no-bundle --ci' or set CANOE_APP_LINUX_BIN=/absolute/path/to/canoe-boot-manager (required from a git worktree)" >&2; \
		exit 1; \
	}
	cp "$(CANOE_APP_LINUX_BIN)" ./build/toolkit/bin/canoe-boot-manager
	chmod +x ./build/toolkit/bin/canoe-boot-manager
build: submodule_uefi submodule_patcher submodule_mode2_profile submodule_abl_tzmap submodule_canoe_bootmgr submodule_canoe_ext4 submodule_ablfvextractor submodule_vbmetafixer canoe_manager canoe_app
	# zip
	cd build/toolkit && zip -r ../toolkit_linux.zip ./
