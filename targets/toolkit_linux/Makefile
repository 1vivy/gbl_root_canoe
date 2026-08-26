.PHONY: clean copy_resources submodule_uefi submodule_patcher \
	submodule_mode2_profile submodule_abl_tzmap submodule_ablfvextractor \
	submodule_vbmetafixer canoe_device_script canoe_host build

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
		if [ ! -f build/ArbTools.efi ] || \
		   [ ! -f build/BLTools.efi ] || [ ! -f build/RebootTools.efi ]; then make tools; fi
	cp ../../submodules/uefi/build/BDS.efi ./build/toolkit/BDS.efi
	cp ../../submodules/uefi/build/ArbTools.efi ./build/toolkit/efisp/tools/ArbTools.efi
	cp ../../submodules/uefi/build/BLTools.efi ./build/toolkit/efisp/tools/BLTools.efi
	cp ../../submodules/uefi/build/RebootTools.efi ./build/toolkit/efisp/tools/RebootTools.efi
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
submodule_ablfvextractor: copy_resources
	cd ../../submodules/ablfvextractor && make build
	cp ../../submodules/ablfvextractor/build/extractfv ./build/toolkit/bin/extractfv
submodule_vbmetafixer: copy_resources
	# vbmetaport grafts an official vbmeta onto a third-party image; vbmetabackup
	# -f lifts that vbmeta out of a local firmware image. `canoe prep` needs both.
	# Guarded like submodule_uefi: the vbmetafixer `build` target is not phony, so
	# a stale build/ directory would otherwise make a plain `make build` a no-op.
	cd ../../tools/vbmetafixer && \
		if [ ! -f build/toolkit/bin/vbmetaport ] || \
		   [ ! -f build/toolkit/bin/vbmetabackup ]; then $(MAKE) clean build; fi
	cp ../../tools/vbmetafixer/build/toolkit/bin/vbmetaport ./build/toolkit/bin/vbmetaport
	cp ../../tools/vbmetafixer/build/toolkit/bin/vbmetabackup ./build/toolkit/bin/vbmetabackup
canoe_device_script: copy_resources
	# The device-side install transaction is shared by the Linux and Windows
	# drivers, so it lives in tools/canoe-device and is copied into each toolkit
	# rather than duplicated per target.
	cp ../../tools/canoe-device/canoe_device_install.sh ./build/toolkit/canoe_device_install.sh
	chmod +x ./build/toolkit/canoe_device_install.sh
	cp ../../tools/canoe-device/README.canoe.md ./build/toolkit/README.canoe.md
canoe_host: copy_resources
	cp -r ../../tools/canoe-host/canoelib ./build/toolkit/canoelib
	find ./build/toolkit/canoelib -type d -name __pycache__ -prune -exec rm -rf {} +
	cp ../../tools/canoe-host/canoe ./build/toolkit/canoe
	chmod +x ./build/toolkit/canoe
build: submodule_uefi submodule_patcher submodule_mode2_profile submodule_abl_tzmap submodule_ablfvextractor submodule_vbmetafixer canoe_device_script canoe_host
	# zip
	cd build/toolkit && zip -r ../toolkit_linux.zip ./
