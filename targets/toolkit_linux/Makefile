.PHONY: clean copy_resources submodule_uefi submodule_patcher \
	submodule_mode2_profile submodule_mode2_profile_android \
	submodule_abl_tzmap submodule_ablfvextractor \
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
	# Build the EDK2 tree at most once per workspace. A repeated `make build`
	# relinks LinuxLoader with different bytes, which breaks the CI check that
	# every package ships a byte-identical BDS.efi. Set UEFI_REBUILD=1 to force a
	# rebuild after editing UEFI sources: `UEFI_REBUILD=1 make target_<name>`
	# from the REPO ROOT, which discards the artifacts once for the whole
	# invocation. Forcing per package here would relink BDS once per package.
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
submodule_mode2_profile_android: copy_resources
	# The arm64 build is pushed on-device by `canoe_stage --mode` for the
	# preferred-mode write. Skipped with a loud warning when the NDK is absent
	# so a host-only local build still packages; CI always has it.
	if [ -n "$(NDK_PATH)" ]; then \
		CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER="$(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang" cargo build --locked --release --target aarch64-linux-android --manifest-path ../../tools/mode2-profile/Cargo.toml && \
		test -f ../../tools/mode2-profile/target/aarch64-linux-android/release/mode2_profile && \
		cp ../../tools/mode2-profile/target/aarch64-linux-android/release/mode2_profile ./build/toolkit/bin/mode2_profile-arm64; \
	else \
		echo "WARNING: NDK_PATH unset; shipping without bin/mode2_profile-arm64 (canoe_stage --mode will be unavailable)"; \
	fi
submodule_abl_tzmap: copy_resources
	cargo build --locked --release --manifest-path ../../tools/abl-tzmap/Cargo.toml
	test -x ../../tools/abl-tzmap/target/release/abl_tzmap
	cp ../../tools/abl-tzmap/target/release/abl_tzmap ./build/toolkit/bin/abl_tzmap
submodule_ablfvextractor: copy_resources
	cd ../../submodules/ablfvextractor && make build
	cp ../../submodules/ablfvextractor/build/extractfv ./build/toolkit/bin/extractfv
submodule_vbmetafixer: copy_resources
	# vbmetaport grafts an official vbmeta onto a third-party image; vbmetabackup
	# -f lifts that vbmeta out of a local firmware image. canoe_prep needs both.
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
	# Host-side Canoe tools are shared by Linux and Windows and copied from
	# one source tree so the two toolkits cannot drift.
	cp -r ../../tools/canoe-host/canoe ./build/toolkit/canoe
	find ./build/toolkit/canoe -type d -name __pycache__ -prune -exec rm -rf {} +
	cp ../../tools/canoe-host/canoe_build \
		../../tools/canoe-host/canoe_prep \
		../../tools/canoe-host/canoe_prep_device \
		../../tools/canoe-host/canoe_stage \
		./build/toolkit/
	chmod +x ./build/toolkit/canoe_build \
		./build/toolkit/canoe_prep \
		./build/toolkit/canoe_prep_device \
		./build/toolkit/canoe_stage
build: submodule_uefi submodule_patcher submodule_mode2_profile submodule_mode2_profile_android submodule_abl_tzmap submodule_ablfvextractor submodule_vbmetafixer canoe_device_script canoe_host
	# zip
	cd build/toolkit && zip -r ../toolkit_linux.zip ./
