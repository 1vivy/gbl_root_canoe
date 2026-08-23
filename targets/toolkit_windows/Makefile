.PHONY: clean copy_resources submodule_uefi submodule_patcher \
	submodule_mode2_profile submodule_mode2_profile_android \
	submodule_abl_tzmap submodule_ablfvextractor \
	submodule_vbmetafixer canoe_device_script build

NDK_PATH ?= $(ANDROID_NDK_LATEST_HOME)
export NDK_PATH

clean:
	rm -rf build || true
copy_resources:
	rm -rf build/toolkit build/toolkit_windows.zip
	mkdir -p build/toolkit
	mkdir -p build/toolkit/bin
	cp -r resources/* build/toolkit/
submodule_uefi: copy_resources
	# Build the EDK2 tree at most once per workspace. A repeated `make build`
	# relinks LinuxLoader with different bytes, which breaks the CI check that
	# every package ships a byte-identical BDS.efi. Set UEFI_REBUILD=1 to force a
	# rebuild after editing UEFI sources.
	# BDS and the standalone tools are guarded SEPARATELY. Rebuilding the tools
	# must not drag in a second `make build`, because that relinks LinuxLoader
	# with different bytes and breaks the byte-identical BDS.efi invariant.
	cd ../../submodules/uefi && \
		if [ "$(UEFI_REBUILD)" = 1 ] || [ ! -f build/BDS.efi ]; then make build; fi && \
		if [ "$(UEFI_REBUILD)" = 1 ] || [ ! -f build/ArbTools.efi ] || \
		   [ ! -f build/BLTools.efi ] || [ ! -f build/RebootTools.efi ]; then make tools; fi
	cp ../../submodules/uefi/build/BDS.efi ./build/toolkit/BDS.efi
	cp ../../submodules/uefi/build/ArbTools.efi ./build/toolkit/efisp/tools/ArbTools.efi
	cp ../../submodules/uefi/build/BLTools.efi ./build/toolkit/efisp/tools/BLTools.efi
	cp ../../submodules/uefi/build/RebootTools.efi ./build/toolkit/efisp/tools/RebootTools.efi
submodule_patcher: copy_resources
	cd ../../submodules/patcher && make build_windows
	cp ../../submodules/patcher/build/patch_abl.exe ./build/toolkit/bin/patch_abl.exe
submodule_mode2_profile: copy_resources
	CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER=x86_64-w64-mingw32-gcc cargo build --locked --release --target x86_64-pc-windows-gnu --manifest-path ../../tools/mode2-profile/Cargo.toml
	test -f ../../tools/mode2-profile/target/x86_64-pc-windows-gnu/release/mode2_profile.exe
	cp ../../tools/mode2-profile/target/x86_64-pc-windows-gnu/release/mode2_profile.exe ./build/toolkit/bin/mode2_profile.exe
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
	CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER=x86_64-w64-mingw32-gcc cargo build --locked --release --target x86_64-pc-windows-gnu --manifest-path ../../tools/abl-tzmap/Cargo.toml
	test -f ../../tools/abl-tzmap/target/x86_64-pc-windows-gnu/release/abl_tzmap.exe
	cp ../../tools/abl-tzmap/target/x86_64-pc-windows-gnu/release/abl_tzmap.exe ./build/toolkit/bin/abl_tzmap.exe
submodule_ablfvextractor: copy_resources
	cd ../../submodules/ablfvextractor && make build_windows
	cp ../../submodules/ablfvextractor/build/extractfv.exe ./build/toolkit/bin/extractfv.exe
submodule_vbmetafixer: copy_resources
	# vbmetaport grafts an official vbmeta onto a third-party image; vbmetabackup
	# -f lifts that vbmeta out of a local firmware image. canoe_prep.bat needs both.
	# Guarded like submodule_uefi: the vbmetafixer `build_windows` target is not
	# phony, so a stale build/ directory would otherwise make a plain make build a no-op.
	cd ../../tools/vbmetafixer && \
		if [ ! -f build/toolkit_win/bin/vbmetaport.exe ] || \
		   [ ! -f build/toolkit_win/bin/vbmetabackup.exe ]; then $(MAKE) build_windows; fi
	cp ../../tools/vbmetafixer/build/toolkit_win/bin/vbmetaport.exe ./build/toolkit/bin/vbmetaport.exe
	cp ../../tools/vbmetafixer/build/toolkit_win/bin/vbmetabackup.exe ./build/toolkit/bin/vbmetabackup.exe
canoe_device_script: copy_resources
	# The device-side install transaction is shared by the Linux and Windows
	# drivers, so it lives in tools/canoe-device and is copied into each toolkit
	# rather than duplicated per target.
	cp ../../tools/canoe-device/canoe_device_install.sh ./build/toolkit/canoe_device_install.sh
	chmod +x ./build/toolkit/canoe_device_install.sh
	cp ../../tools/canoe-device/README.canoe.md ./build/toolkit/README.canoe.md
build: submodule_uefi submodule_patcher submodule_mode2_profile submodule_mode2_profile_android submodule_abl_tzmap submodule_ablfvextractor submodule_vbmetafixer canoe_device_script
	# zip
	cd build/toolkit && zip -r ../toolkit_windows.zip ./
