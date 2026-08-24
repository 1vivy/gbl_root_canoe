.PHONY: clean copy_resources submodule_uefi submodule_patcher \
	submodule_mode2_profile submodule_mode2_profile_android \
	submodule_abl_tzmap submodule_ablfvextractor \
	submodule_vbmetafixer canoe_device_script canoe_host python_embed build

NDK_PATH ?= $(ANDROID_NDK_LATEST_HOME)
export NDK_PATH
PYTHON_EMBED_VERSION ?= 3.14.7
PYTHON_EMBED_SHA256 ?= d297e5ff019966817ad8502465176139f2d3d840fa4ed84b13bed399a6ab1f15
PYTHON_EMBED_URL ?= https://www.python.org/ftp/python/$(PYTHON_EMBED_VERSION)/python-$(PYTHON_EMBED_VERSION)-embed-amd64.zip
PYTHON_EMBED_CACHE ?= ../../.cache/python-embed
PYTHON_EMBED_ARCHIVE = $(PYTHON_EMBED_CACHE)/python-$(PYTHON_EMBED_VERSION)-embed-amd64.zip

clean:
	rm -rf build || true
copy_resources:
	rm -rf build/toolkit build/toolkit_windows.zip
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
	# -f lifts that vbmeta out of a local firmware image. canoe_prep needs both.
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
	cp ../../tools/canoe-host/canoe_build.cmd \
		../../tools/canoe-host/canoe_prep.cmd \
		../../tools/canoe-host/canoe_prep_device.cmd \
		../../tools/canoe-host/canoe_stage.cmd \
		./build/toolkit/
python_embed: copy_resources $(PYTHON_EMBED_ARCHIVE)
	@set -eu; \
	expected="$(PYTHON_EMBED_SHA256)"; \
	actual="$$(sha256sum "$(PYTHON_EMBED_ARCHIVE)" | cut -d' ' -f1)"; \
	if [ "$$actual" != "$$expected" ]; then \
		echo "Python embed sha256 mismatch: expected $$expected, got $$actual" >&2; \
		exit 1; \
	fi; \
	echo "Verified Python embed sha256: $$actual"; \
	rm -rf build/toolkit/python; \
	mkdir -p build/toolkit/python; \
	unzip -q "$(PYTHON_EMBED_ARCHIVE)" -d build/toolkit/python; \
	test -f build/toolkit/python/python.exe
$(PYTHON_EMBED_ARCHIVE):
	# GNU wget only. `--fail` and `--location` are curl options and GNU wget
	# rejects them outright; it already exits non-zero on an HTTP error and
	# follows redirects by default, so neither is needed. Download to a temp
	# name and rename, so an interrupted fetch cannot leave a short archive
	# that the checksum step would then have to catch.
	@set -eu; \
	mkdir -p "$(dir $@)"; \
	tmp="$@.tmp"; \
	rm -f "$$tmp"; \
	wget --no-verbose --tries=3 --output-document="$$tmp" "$(PYTHON_EMBED_URL)"; \
	mv "$$tmp" "$@"
build: submodule_uefi submodule_patcher submodule_mode2_profile submodule_mode2_profile_android submodule_abl_tzmap submodule_ablfvextractor submodule_vbmetafixer canoe_device_script canoe_host python_embed
	# zip
	cd build/toolkit && zip -r ../toolkit_windows.zip ./
