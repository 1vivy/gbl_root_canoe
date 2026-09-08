CANOE_MANAGER_SRC ?= $(abspath ../../../canoe-boot-manager/native/canoe-manager)
include ../../version.mk
include ../../imports.mk

.PHONY: canoe_manager clean copy_resources submodule_uefi submodule_patcher \
	submodule_mode2_profile submodule_abl_tzmap submodule_canoe_bootmgr \
	submodule_ablfvextractor submodule_vbmetafixer \
	canoe_manager canoe_app platform_tools ext4_tools build

ROOT_DIR := $(abspath ../..)


PLATFORM_TOOLS_CACHE ?= $(ROOT_DIR)/.cache/platform-tools
PLATFORM_TOOLS_ARCHIVE = $(PLATFORM_TOOLS_CACHE)/platform-tools_$(PLATFORM_TOOLS_VERSION)-win.zip

CANOE_EXT4_WINDOWS ?= $(ROOT_DIR)/tools/canoe-ext4/canoe-ext4.exe
CANOE_APP_WINDOWS_BIN ?= $(ROOT_DIR)/../canoe-boot-manager/src-tauri/target/x86_64-pc-windows-gnu/release/canoe-boot-manager.exe
CANOE_WEBVIEW2_LOADER_WINDOWS ?= $(dir $(CANOE_APP_WINDOWS_BIN))WebView2Loader.dll

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
	cd ../../submodules/patcher && make build_windows
	cp ../../submodules/patcher/build/patch_abl.exe ./build/toolkit/bin/patch_abl.exe
submodule_mode2_profile: copy_resources
	test -d "$$(rustc --print target-libdir --target x86_64-pc-windows-gnu 2>/dev/null)" || { echo "Rust target x86_64-pc-windows-gnu is missing; use rustup cargo first on PATH (for example: PATH=\"\$$HOME/.cargo/bin:\$$PATH\" make target_toolkit_windows) with that target installed." >&2; exit 1; }
	CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER=x86_64-w64-mingw32-gcc cargo build --locked --release --target x86_64-pc-windows-gnu --manifest-path ../../tools/mode2-profile/Cargo.toml
	test -f ../../tools/mode2-profile/target/x86_64-pc-windows-gnu/release/mode2_profile.exe
	cp ../../tools/mode2-profile/target/x86_64-pc-windows-gnu/release/mode2_profile.exe ./build/toolkit/bin/mode2_profile.exe
submodule_abl_tzmap: copy_resources
	CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER=x86_64-w64-mingw32-gcc cargo build --locked --release --target x86_64-pc-windows-gnu --manifest-path ../../tools/abl-tzmap/Cargo.toml
	test -f ../../tools/abl-tzmap/target/x86_64-pc-windows-gnu/release/abl_tzmap.exe
	cp ../../tools/abl-tzmap/target/x86_64-pc-windows-gnu/release/abl_tzmap.exe ./build/toolkit/bin/abl_tzmap.exe
submodule_canoe_bootmgr: copy_resources
	CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER=x86_64-w64-mingw32-gcc cargo build --locked --release --target x86_64-pc-windows-gnu --manifest-path ../../tools/canoe-bootmgr/Cargo.toml
	test -f ../../tools/canoe-bootmgr/target/x86_64-pc-windows-gnu/release/canoe-bootmgr.exe
	cp ../../tools/canoe-bootmgr/target/x86_64-pc-windows-gnu/release/canoe-bootmgr.exe ./build/toolkit/bin/canoe-bootmgr.exe
canoe_manager: copy_resources
	CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER=x86_64-w64-mingw32-gcc cargo build --locked --release --target x86_64-pc-windows-gnu --manifest-path "$(CANOE_MANAGER_SRC)/Cargo.toml"
	test -f "$(CANOE_MANAGER_SRC)/target/x86_64-pc-windows-gnu/release/canoe-manager.exe"
	cp "$(CANOE_MANAGER_SRC)/target/x86_64-pc-windows-gnu/release/canoe-manager.exe" ./build/toolkit/bin/canoe-manager.exe
submodule_ablfvextractor: copy_resources
	cd ../../submodules/ablfvextractor && make build_windows
	cp ../../submodules/ablfvextractor/build/extractfv.exe ./build/toolkit/bin/extractfv.exe
submodule_vbmetafixer: copy_resources
	# vbmetaport grafts an official vbmeta onto a third-party image;
	# vbmetabackup lifts that vbmeta out of a local firmware image.
	# Guarded like submodule_uefi: the vbmetafixer `build_windows` target is not
	# phony, so a stale build/ directory would otherwise make a plain `make build` a no-op.
	cd ../../tools/vbmetafixer && \
		if [ ! -f build/toolkit_win/bin/vbmetaport.exe ] || \
		   [ ! -f build/toolkit_win/bin/vbmetabackup.exe ]; then $(MAKE) build_windows; fi
	cp ../../tools/vbmetafixer/build/toolkit_win/bin/vbmetaport.exe ./build/toolkit/bin/vbmetaport.exe
	cp ../../tools/vbmetafixer/build/toolkit_win/bin/vbmetabackup.exe ./build/toolkit/bin/vbmetabackup.exe
platform_tools: copy_resources
	$(MAKE) -C "$(ROOT_DIR)" fetch-verified \
		FETCH_URL="$(PLATFORM_TOOLS_URL)" \
		FETCH_SHA256="$(PLATFORM_TOOLS_SHA256)" \
		FETCH_DEST="$(PLATFORM_TOOLS_ARCHIVE)"
	@set -eu; \
	tmp="$$(mktemp -d "$(CURDIR)/build/.platform-tools.XXXXXX")"; \
	trap 'rm -rf "$$tmp"' EXIT HUP INT TERM; \
	unzip -q "$(PLATFORM_TOOLS_ARCHIVE)" \
		'platform-tools/fastboot.exe' \
		'platform-tools/AdbWinApi.dll' \
		'platform-tools/AdbWinUsbApi.dll' \
		'platform-tools/source.properties' \
		-d "$$tmp"; \
	mkdir -p "$$tmp/out"; \
	for member in fastboot.exe AdbWinApi.dll AdbWinUsbApi.dll source.properties; do \
		cp "$$tmp/platform-tools/$$member" "$$tmp/out/$$member"; \
	done; \
	rm -rf build/toolkit/Platform-Tools; \
	mv "$$tmp/out" build/toolkit/Platform-Tools; \
	trap - EXIT HUP INT TERM; \
	rm -rf "$$tmp"
ext4_tools: copy_resources
	@test -f "$(CANOE_EXT4_WINDOWS)" || { \
		echo "canoe-ext4 Windows binary is required; run tools/canoe-ext4/build-windows.sh or set CANOE_EXT4_WINDOWS" >&2; \
		exit 1; \
	}
	mkdir -p build/toolkit/bin
	cp "$(CANOE_EXT4_WINDOWS)" ./build/toolkit/bin/canoe-ext4.exe
canoe_app: copy_resources
	@test -f "$(CANOE_APP_WINDOWS_BIN)" || { \
		echo "canoe-boot-manager Windows binary is required; run 'cd ../canoe-boot-manager && bunx tauri build --target x86_64-pc-windows-gnu --no-bundle --ci' or set CANOE_APP_WINDOWS_BIN=/absolute/path/to/canoe-boot-manager.exe (required from a git worktree)" >&2; \
		exit 1; \
	}
	@test -f "$(CANOE_WEBVIEW2_LOADER_WINDOWS)" || { \
		echo "WebView2Loader.dll is required beside the GNU-target canoe-boot-manager.exe; rebuild the Tauri app or set CANOE_WEBVIEW2_LOADER_WINDOWS=/absolute/path/to/WebView2Loader.dll" >&2; \
		exit 1; \
	}
	mkdir -p build/toolkit/bin
	cp "$(CANOE_APP_WINDOWS_BIN)" ./build/toolkit/bin/canoe-boot-manager.exe
	cp "$(CANOE_WEBVIEW2_LOADER_WINDOWS)" ./build/toolkit/bin/WebView2Loader.dll
build: submodule_uefi submodule_patcher submodule_mode2_profile submodule_abl_tzmap submodule_canoe_bootmgr canoe_manager submodule_ablfvextractor submodule_vbmetafixer platform_tools ext4_tools canoe_app
	cd build/toolkit && zip -r ../toolkit_windows.zip ./
