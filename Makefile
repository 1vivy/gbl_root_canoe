.PHONY: clean clean_submodules targets_clean \
	submodule_uefi_clean submodule_patcher_clean submodule_ablfvextractor_clean \
	target_toolkit_windows_clean target_toolkit_linux_clean \
	target_magisk_module_clean target_toolkit_android_clean \
	target_toolkit_windows target_toolkit_linux target_magisk_module \
	target_toolkit_android dev_target_extract_and_patch \
	tools_vbmetafixer_linux tools_vbmetafixer_windows \
	tools_vbmetafixer_android test uefi_discard fetch-verified

# UEFI_REBUILD=1 forces a from-scratch BDS, ONCE for the whole invocation.
#
# Once, not once per package, because the EDK2 build is not reproducible:
# building the same sources clean twice in a row was measured to give
# BDS.efi sha256 7ef5d010... and then 09a83e86.... Rebuilding per package would
# therefore put different bytes in each archive and break the CI check that
# every package carries byte-identical boot artifacts. Dropping the tree here
# makes the first package that runs do the single build; the rest find the
# artifacts present and reuse them.
#
# It is a full clean, not just a delete of build/*.efi, because
# submodules/uefi's `build` target removes edk2's LinuxLoader.efi before
# invoking EDK2, and EDK2 declines to regenerate a module it considers
# up to date - so against a warm Build tree with no source change the rebuild
# ends with no artifact at all. A clean makes the rebuild unconditional.
#
# Without this target, editing a UEFI source and running `make target_<name>`
# silently packages the previous build, which is how a release ships a boot
# menu stamped with the version before it.
ifeq ($(UEFI_REBUILD),1)
target_toolkit_windows target_toolkit_linux target_magisk_module \
target_toolkit_android: uefi_discard
uefi_discard:
	$(MAKE) -C submodules/uefi clean
endif

submodule_uefi_clean:
	cd submodules/uefi && make clean
submodule_patcher_clean:
	cd submodules/patcher && make clean
submodule_ablfvextractor_clean:
	cd submodules/ablfvextractor && make clean
clean_submodules: submodule_uefi_clean submodule_patcher_clean submodule_ablfvextractor_clean

target_toolkit_windows_clean:
	cd targets/toolkit_windows && make clean
target_toolkit_linux_clean:
	cd targets/toolkit_linux && make clean
target_magisk_module_clean:
	cd targets/magisk_module && make clean
target_toolkit_android_clean:
	cd targets/toolkit_android && make clean
targets_clean: clean_submodules target_toolkit_windows_clean target_toolkit_linux_clean target_magisk_module_clean target_toolkit_android_clean

clean: targets_clean clean_submodules

target_toolkit_windows:
	cd targets/toolkit_windows && $(MAKE) build
target_toolkit_linux:
	cd targets/toolkit_linux && $(MAKE) build
target_magisk_module:
	cd targets/magisk_module && make build
target_toolkit_android:
	cd targets/toolkit_android && make build

dev_target_extract_and_patch:
	cd dev_targets/extract_and_patch && make patch

# tools not dependency of main project, build separately
tools_vbmetafixer_linux:
	cd tools/vbmetafixer && make build
tools_vbmetafixer_windows:
	cd tools/vbmetafixer && make build_windows
tools_vbmetafixer_android:
	cd tools/vbmetafixer && make build_android

test:
	cargo test --locked --manifest-path tools/mode2-profile/Cargo.toml
	cargo test --locked --manifest-path tools/abl-tzmap/Cargo.toml
	$(MAKE) -C submodules/patcher test
	$(MAKE) -C submodules/uefi test
	sh targets/magisk_module/tests/test_flows.sh
	sh targets/magisk_module/tests/test_webui.sh
	sh tools/canoe-device/tests/test_canoe_boot_entry.sh
	sh tools/canoe-device/tests/test_canoe_device_install.sh
	sh targets/toolkit_android/tests/test_build_script.sh
	python3 -m pytest tools/canoe-host

# Download a pinned package asset without ever exposing a partial archive to
# subsequent builds. Package Makefiles invoke this target with absolute
# FETCH_DEST paths so the mechanism is independent of their working directory.
fetch-verified:
	@set -eu; \
	test -n "$(FETCH_URL)" || { echo "FETCH_URL is required" >&2; exit 2; }; \
	test -n "$(FETCH_SHA256)" || { echo "FETCH_SHA256 is required" >&2; exit 2; }; \
	test -n "$(FETCH_DEST)" || { echo "FETCH_DEST is required" >&2; exit 2; }; \
	dest="$(FETCH_DEST)"; \
	mkdir -p "$$(dirname "$$dest")"; \
	tmp="$$dest.tmp"; \
	trap 'rm -f "$$tmp"' EXIT HUP INT TERM; \
	if [ -f "$$dest" ] && \
	   [ "$$(sha256sum "$$dest" | cut -d" " -f1)" = "$(FETCH_SHA256)" ]; then \
		echo "Verified $$(basename "$$dest") sha256: $(FETCH_SHA256)"; \
		exit 0; \
	fi; \
	rm -f "$$dest" "$$tmp"; \
	wget --no-verbose --tries=3 --output-document="$$tmp" "$(FETCH_URL)"; \
	actual="$$(sha256sum "$$tmp" | cut -d" " -f1)"; \
	if [ "$$actual" != "$(FETCH_SHA256)" ]; then \
		echo "sha256 mismatch for $$(basename "$$dest"): expected $(FETCH_SHA256), got $$actual" >&2; \
		exit 1; \
	fi; \
	mv "$$tmp" "$$dest"; \
	trap - EXIT HUP INT TERM; \
	echo "Downloaded and verified $$(basename "$$dest") sha256: $$actual"
