.PHONY: clean copy_resources submodule_uefi submodule_patcher \
	submodule_mode2_profile submodule_abl_tzmap submodule_ablfvextractor \
	submodule_vbmetafixer canoe_host python_embed platform_tools ext4_tools build

ROOT_DIR := $(abspath ../..)

PYTHON_EMBED_VERSION ?= 3.14.7
PYTHON_EMBED_SHA256 ?= d297e5ff019966817ad8502465176139f2d3d840fa4ed84b13bed399a6ab1f15
PYTHON_EMBED_URL ?= https://www.python.org/ftp/python/$(PYTHON_EMBED_VERSION)/python-$(PYTHON_EMBED_VERSION)-embed-amd64.zip
PYTHON_EMBED_CACHE ?= $(ROOT_DIR)/.cache/python-embed
PYTHON_EMBED_ARCHIVE = $(PYTHON_EMBED_CACHE)/python-$(PYTHON_EMBED_VERSION)-embed-amd64.zip

PLATFORM_TOOLS_URL ?= https://dl.google.com/android/repository/platform-tools_r37.0.0-win.zip
PLATFORM_TOOLS_SHA256 ?= 4fe305812db074cea32903a489d061eb4454cbc90a49e8fea677f4b7af764918
PLATFORM_TOOLS_CACHE ?= $(ROOT_DIR)/.cache/platform-tools
PLATFORM_TOOLS_ARCHIVE = $(PLATFORM_TOOLS_CACHE)/platform-tools_r37.0.0-win.zip

EXT4_WINDOWS_URL ?= https://github.com/Mateuscruz19/Ext4Windows/releases/download/v1.2.0/Ext4Windows-v1.2.0-portable.zip
EXT4_WINDOWS_SHA256 ?= 1dba3d4dcc8a3c7bb3d8837bbe8d91e8984b8d0e98f6c5ecf413fb3bedcc853e
EXT4_WINDOWS_CACHE ?= $(ROOT_DIR)/.cache/ext4windows
EXT4_WINDOWS_ARCHIVE = $(EXT4_WINDOWS_CACHE)/Ext4Windows-v1.2.0-portable.zip

WINFSP_URL ?= https://github.com/winfsp/winfsp/releases/download/v2.1/winfsp-2.1.25156.msi
WINFSP_SHA256 ?= 073a70e00f77423e34bed98b86e600def93393ba5822204fac57a29324db9f7a
WINFSP_CACHE ?= $(ROOT_DIR)/.cache/winfsp
WINFSP_ARCHIVE = $(WINFSP_CACHE)/winfsp-2.1.25156.msi

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
	mkdir -p build/toolkit/efisp/tools
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
submodule_abl_tzmap: copy_resources
	CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER=x86_64-w64-mingw32-gcc cargo build --locked --release --target x86_64-pc-windows-gnu --manifest-path ../../tools/abl-tzmap/Cargo.toml
	test -f ../../tools/abl-tzmap/target/x86_64-pc-windows-gnu/release/abl_tzmap.exe
	cp ../../tools/abl-tzmap/target/x86_64-pc-windows-gnu/release/abl_tzmap.exe ./build/toolkit/bin/abl_tzmap.exe
submodule_ablfvextractor: copy_resources
	cd ../../submodules/ablfvextractor && make build_windows
	cp ../../submodules/ablfvextractor/build/extractfv.exe ./build/toolkit/bin/extractfv.exe
submodule_vbmetafixer: copy_resources
	# vbmetaport grafts an official vbmeta onto a third-party image;
	# vbmetabackup lifts that vbmeta out of a local firmware image.
	# Guarded like submodule_uefi: the vbmetafixer `build_windows` target is not
	# phony, so a stale build/ directory would otherwise make a plain make build a no-op.
	cd ../../tools/vbmetafixer && \
		if [ ! -f build/toolkit_win/bin/vbmetaport.exe ] || \
		   [ ! -f build/toolkit_win/bin/vbmetabackup.exe ]; then $(MAKE) build_windows; fi
	cp ../../tools/vbmetafixer/build/toolkit_win/bin/vbmetaport.exe ./build/toolkit/bin/vbmetaport.exe
	cp ../../tools/vbmetafixer/build/toolkit_win/bin/vbmetabackup.exe ./build/toolkit/bin/vbmetabackup.exe
canoe_host: copy_resources
	# Keep one user-facing launcher; the package is an internal implementation detail.
	cp -r ../../tools/canoe-host/canoelib ./build/toolkit/canoelib
	find ./build/toolkit/canoelib -type d -name __pycache__ -prune -exec rm -rf {} +
	cp ../../tools/canoe-host/canoe ./build/toolkit/canoe
	cp ../../tools/canoe-host/canoe.cmd ./build/toolkit/canoe.cmd
	chmod +x ./build/toolkit/canoe
python_embed: copy_resources
	$(MAKE) -C "$(ROOT_DIR)" fetch-verified \
		FETCH_URL="$(PYTHON_EMBED_URL)" \
		FETCH_SHA256="$(PYTHON_EMBED_SHA256)" \
		FETCH_DEST="$(PYTHON_EMBED_ARCHIVE)"
	@set -eu; \
	rm -rf build/toolkit/python; \
	mkdir -p build/toolkit/python; \
	unzip -q "$(PYTHON_EMBED_ARCHIVE)" -d build/toolkit/python; \
	test -f build/toolkit/python/python.exe
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
		'platform-tools/libwinpthread-1.dll' \
		'platform-tools/source.properties' \
		-d "$$tmp"; \
	mkdir -p "$$tmp/out"; \
	for member in fastboot.exe AdbWinApi.dll AdbWinUsbApi.dll libwinpthread-1.dll source.properties; do \
		cp "$$tmp/platform-tools/$$member" "$$tmp/out/$$member"; \
	done; \
	rm -rf build/toolkit/Platform-Tools; \
	mv "$$tmp/out" build/toolkit/Platform-Tools; \
	trap - EXIT HUP INT TERM; \
	rm -rf "$$tmp"
ext4_tools: copy_resources
	$(MAKE) -C "$(ROOT_DIR)" fetch-verified \
		FETCH_URL="$(EXT4_WINDOWS_URL)" \
		FETCH_SHA256="$(EXT4_WINDOWS_SHA256)" \
		FETCH_DEST="$(EXT4_WINDOWS_ARCHIVE)"
	$(MAKE) -C "$(ROOT_DIR)" fetch-verified \
		FETCH_URL="$(WINFSP_URL)" \
		FETCH_SHA256="$(WINFSP_SHA256)" \
		FETCH_DEST="$(WINFSP_ARCHIVE)"
	@set -eu; \
	tmp="$$(mktemp -d "$(CURDIR)/build/.ext4-tools.XXXXXX")"; \
	trap 'rm -rf "$$tmp"' EXIT HUP INT TERM; \
	unzip -q "$(EXT4_WINDOWS_ARCHIVE)" \
		'ext4windows.exe' 'winfsp-x64.dll' -d "$$tmp"; \
	test -f "$$tmp/ext4windows.exe"; \
	test -f "$$tmp/winfsp-x64.dll"; \
	mkdir -p "$$tmp/out"; \
	cp "$$tmp/ext4windows.exe" "$$tmp/out/ext4windows.exe"; \
	cp "$$tmp/winfsp-x64.dll" "$$tmp/out/winfsp-x64.dll"; \
	cp "$(WINFSP_ARCHIVE)" "$$tmp/out/winfsp-2.1.25156.msi"; \
	printf '%s\n' \
		'These binaries are redistributed unmodified. Their upstream sources and' \
		'licences, so the package can be audited without network access:' \
		'' \
		'ext4windows.exe  Ext4Windows v1.2.0, GPL-2.0' \
		'                 $(EXT4_WINDOWS_URL)' \
		'                 sha256 $(EXT4_WINDOWS_SHA256)' \
		'' \
		'winfsp-x64.dll   shipped inside the Ext4Windows portable archive above;' \
		'winfsp-*.msi     WinFsp 2.1, under its own licence' \
		'                 $(WINFSP_URL)' \
		'                 sha256 $(WINFSP_SHA256)' \
		'' \
		'ext4windows.exe mounts read-only unless --rw is passed. canoe install' \
		'passes --rw; a manual mount must too, or the boot root cannot be written.' \
		> "$$tmp/out/SOURCES.txt"; \
	rm -rf build/toolkit/ext4; \
	mv "$$tmp/out" build/toolkit/ext4; \
	trap - EXIT HUP INT TERM; \
	rm -rf "$$tmp"
build: submodule_uefi submodule_patcher submodule_mode2_profile submodule_abl_tzmap submodule_ablfvextractor submodule_vbmetafixer canoe_host python_embed platform_tools ext4_tools
	cd build/toolkit && zip -r ../toolkit_windows.zip ./
