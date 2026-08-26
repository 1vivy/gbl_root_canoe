.PHONY: clean copy_resources submodule_uefi submodule_patcher \
	submodule_mode2_profile submodule_abl_tzmap submodule_ablfvextractor build

NDK_PATH ?= $(ANDROID_NDK_LATEST_HOME)
export NDK_PATH

clean:
	rm -rf build || true
copy_resources:
	rm -rf build/toolkit build/toolkit_android.zip
	mkdir -p build/toolkit
	mkdir -p build/toolkit/bin
	cp -r resources/* build/toolkit/
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
		if [ ! -f build/ArbTools.efi ] || \
		   [ ! -f build/BLTools.efi ] || [ ! -f build/RebootTools.efi ]; then make tools; fi
	cp ../../submodules/uefi/build/BDS.efi ./build/toolkit/BDS.efi
	mkdir -p build/toolkit/efisp/tools
	cp ../../submodules/uefi/build/ArbTools.efi ./build/toolkit/efisp/tools/ArbTools.efi
	cp ../../submodules/uefi/build/BLTools.efi ./build/toolkit/efisp/tools/BLTools.efi
	cp ../../submodules/uefi/build/RebootTools.efi ./build/toolkit/efisp/tools/RebootTools.efi
submodule_patcher: copy_resources
	cd ../../submodules/patcher && make build_android
	cp ../../submodules/patcher/build/patch_abl_android ./build/toolkit/bin/patch_abl
submodule_mode2_profile: copy_resources
	test -n "$(NDK_PATH)" || { echo "NDK_PATH is required for mode2_profile"; exit 1; }
	test -x "$(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang" || { echo "Android NDK clang is unavailable"; exit 1; }
	CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER="$(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang" cargo build --locked --release --target aarch64-linux-android --manifest-path ../../tools/mode2-profile/Cargo.toml
	test -f ../../tools/mode2-profile/target/aarch64-linux-android/release/mode2_profile
	cp ../../tools/mode2-profile/target/aarch64-linux-android/release/mode2_profile ./build/toolkit/bin/mode2_profile
submodule_abl_tzmap: copy_resources
	test -n "$(NDK_PATH)" || { echo "NDK_PATH is required for abl_tzmap"; exit 1; }
	test -x "$(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang" || { echo "Android NDK clang is unavailable"; exit 1; }
	CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER="$(NDK_PATH)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang" cargo build --locked --release --target aarch64-linux-android --manifest-path ../../tools/abl-tzmap/Cargo.toml
	test -f ../../tools/abl-tzmap/target/aarch64-linux-android/release/abl_tzmap
	cp ../../tools/abl-tzmap/target/aarch64-linux-android/release/abl_tzmap ./build/toolkit/bin/abl_tzmap
submodule_ablfvextractor: copy_resources
	cd ../../submodules/ablfvextractor && make build_android
	cp ../../submodules/ablfvextractor/build/extractfv_android ./build/toolkit/bin/extractfv
build: submodule_uefi submodule_patcher submodule_mode2_profile submodule_abl_tzmap submodule_ablfvextractor
	# zip
	cd build/toolkit && zip -r ../toolkit_android.zip ./
