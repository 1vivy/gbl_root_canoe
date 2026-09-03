CANOE_ROOT_DIR := $(abspath .)
include version.mk
include imports.mk

.PHONY: clean clean_submodules targets_clean \
	submodule_uefi_clean submodule_patcher_clean submodule_ablfvextractor_clean \
	target_toolkit_windows_clean target_toolkit_linux_clean \
	target_magisk_module_clean target_toolkit_android_clean \
	target_toolkit_windows target_toolkit_linux target_magisk_module \
	target_toolkit_android dev_target_extract_and_patch \
	tools_vbmetafixer_linux tools_vbmetafixer_windows \
	tools_vbmetafixer_android test uefi_discard fetch-verified \
	bump version-check imports import-pin

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

bump:
	@set -eu; \
	version='$(if $(VERSION),$(VERSION),$(CANOE_VERSION))'; \
	if [ "$(CANOE_NONRELEASE)" = "1" ]; then \
		printf 'make bump refuses non-release CANOE_VERSION override %s\n' "$$version" >&2; \
		exit 2; \
	fi; \
	version_code='$(if $(VERSION_CODE),$(VERSION_CODE),$(CANOE_VERSION_CODE))'; \
	awk -v value="$$version" 'BEGIN { exit !(value ~ /^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$$/) }' || { \
		printf 'Invalid VERSION: %s\n' "$$version" >&2; \
		exit 2; \
	}; \
	if [ "$$version" = "0.0.0-dev" ]; then \
		printf 'Invalid VERSION: 0.0.0-dev is reserved for unstamped-build detection\n' >&2; \
		exit 2; \
	fi; \
	awk -v value="$$version_code" 'BEGIN { exit !(value ~ /^[0-9]+$$/) }' || { \
		printf 'Invalid VERSION_CODE: %s\n' "$$version_code" >&2; \
		exit 2; \
	}; \
	mkdir -p tools/canoe/src; \
	trap 'rm -f version.mk.tmp imports.mk.tmp tools/canoe/src/version.rs.tmp targets/magisk_module/module/module.prop.tmp README.md.tmp README_zh.md.tmp wiki/docs/canoe-cfg.md.tmp wiki/docs/zh/canoe-cfg.md.tmp wiki/docs/release.md.tmp wiki/docs/zh/release.md.tmp' EXIT HUP INT TERM; \
	printf '%s\n' \
		'# Single source of truth for Canoe versions.' \
		'# `make bump` regenerates every derived file.' \
		'# `make version-check` fails when they drift.' \
		'# Direct CANOE_VERSION overrides are refused unless an explicit non-release opt-in is present.' \
		'_CANOE_VERSION_SOURCE := $$(shell sed -n "/^[[:space:]]*CANOE_VERSION[[:space:]]*=/ { s/^[[:space:]]*CANOE_VERSION[[:space:]]*=[[:space:]]*//; p; q; }" $$(lastword $$(MAKEFILE_LIST)))' \
		'CANOE_VERSION = '"$$version" \
		'CANOE_VERSION_CODE = '"$$version_code" \
		'CANOE_VERSION_OVERRIDE ?= 0' \
		'ifneq ($$(origin CANOE_VERSION),file)' \
		'ifneq ($$(CANOE_VERSION),$$(_CANOE_VERSION_SOURCE))' \
		'ifneq ($$(CANOE_VERSION_OVERRIDE),1)' \
		'$$(error CANOE_VERSION override refused: canonical $$(_CANOE_VERSION_SOURCE), got $$(CANOE_VERSION); set CANOE_VERSION_OVERRIDE=1 only for a non-release build)' \
		'endif' \
		'ifneq ($$(origin CANOE_VERSION_OVERRIDE),command line)' \
		'$$(error CANOE_VERSION override refused: CANOE_VERSION_OVERRIDE=1 must be supplied on the make command line)' \
		'endif' \
		'ifneq ($$(filter %-local,$$(CANOE_VERSION)),)' \
		'CANOE_NONRELEASE := 1' \
		'else' \
		'override CANOE_VERSION := $$(CANOE_VERSION)-local' \
		'CANOE_NONRELEASE := 1' \
		'endif' \
		'endif' \
		'endif' > version.mk.tmp; \
	if ! cmp -s version.mk.tmp version.mk; then mv version.mk.tmp version.mk; else rm version.mk.tmp; fi; \
	python3 scripts/imports.py mk > imports.mk.tmp; \
	if ! cmp -s imports.mk.tmp imports.mk; then mv imports.mk.tmp imports.mk; else rm imports.mk.tmp; fi; \
	printf '%s\n' \
		'//! Build version generated from version.mk by `make bump`.' \
		'//!' \
		'//! `make version-check` verifies that this generated module stays synchronized.' \
		'' \
		"pub const VERSION: &str = \"$$version\";" > tools/canoe/src/version.rs.tmp; \
	if ! cmp -s tools/canoe/src/version.rs.tmp tools/canoe/src/version.rs; then \
		mv tools/canoe/src/version.rs.tmp tools/canoe/src/version.rs; \
	else \
		rm tools/canoe/src/version.rs.tmp; \
	fi; \
	sed -e "s/^version=.*/version=$$version/" \
		-e "s/^versionCode=.*/versionCode=$$version_code/" \
		targets/magisk_module/module/module.prop > targets/magisk_module/module/module.prop.tmp; \
	if ! cmp -s targets/magisk_module/module/module.prop.tmp targets/magisk_module/module/module.prop; then \
		mv targets/magisk_module/module/module.prop.tmp targets/magisk_module/module/module.prop; \
	else \
		rm targets/magisk_module/module/module.prop.tmp; \
	fi; \
	sed -E \
		-e "s/^(The )?(current|[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?) release surface/The $$version release surface/" \
		README.md > README.md.tmp; \
	sed -E \
		-e "s/^(当前|[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?)[[:space:]]*(的)?发布界面/$$version 的发布界面/" \
		README_zh.md > README_zh.md.tmp; \
	sed -E \
		-e "s/(In )([0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?)( the boot policy)/\\1$$version\\5/" \
		-e "s/^The ([0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?) writer manages/The $$version writer manages/" \
		wiki/docs/canoe-cfg.md > wiki/docs/canoe-cfg.md.tmp; \
	sed -E \
		-e "s/^([^。]*。)([0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?)( 使用显式启动策略)/\\1$$version\\5/" \
		-e "s/^([0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?) 为每个已安装槽位管理一组三件套/$$version 为每个已安装槽位管理一组三件套/" \
		wiki/docs/zh/canoe-cfg.md > wiki/docs/zh/canoe-cfg.md.tmp; \
	if ! cmp -s wiki/docs/canoe-cfg.md.tmp wiki/docs/canoe-cfg.md; then mv wiki/docs/canoe-cfg.md.tmp wiki/docs/canoe-cfg.md; else rm wiki/docs/canoe-cfg.md.tmp; fi; \
	if ! cmp -s wiki/docs/zh/canoe-cfg.md.tmp wiki/docs/zh/canoe-cfg.md; then mv wiki/docs/zh/canoe-cfg.md.tmp wiki/docs/zh/canoe-cfg.md; else rm wiki/docs/zh/canoe-cfg.md.tmp; fi; \
	sed -E \
		-e "s/^(make bump VERSION=)[^ ]+( VERSION_CODE=)[^ ]+$$/\\1$$version\\2$$version_code/" \
		wiki/docs/release.md > wiki/docs/release.md.tmp; \
	sed -E \
		-e "s/^(make bump VERSION=)[^ ]+( VERSION_CODE=)[^ ]+$$/\\1$$version\\2$$version_code/" \
		wiki/docs/zh/release.md > wiki/docs/zh/release.md.tmp; \
	if ! cmp -s README.md.tmp README.md; then mv README.md.tmp README.md; else rm README.md.tmp; fi; \
	if ! cmp -s README_zh.md.tmp README_zh.md; then mv README_zh.md.tmp README_zh.md; else rm README_zh.md.tmp; fi; \
	if ! cmp -s wiki/docs/release.md.tmp wiki/docs/release.md; then mv wiki/docs/release.md.tmp wiki/docs/release.md; else rm wiki/docs/release.md.tmp; fi; \
	if ! cmp -s wiki/docs/zh/release.md.tmp wiki/docs/zh/release.md; then mv wiki/docs/zh/release.md.tmp wiki/docs/zh/release.md; else rm wiki/docs/zh/release.md.tmp; fi; \
	printf 'Bumped Canoe to %s (version code %s)\n' "$$version" "$$version_code"

imports:
	python3 scripts/imports.py list

import-pin:
	@set -eu; \
	test -n "$(ID)" || { printf 'ID is required (usage: make import-pin ID=<id> [VERSION=<version>])\n' >&2; exit 2; }; \
	trap 'rm -f imports.mk.tmp' EXIT HUP INT TERM; \
	if [ -n "$(VERSION)" ]; then \
		python3 scripts/imports.py pin "$(ID)" --version "$(VERSION)"; \
	else \
		python3 scripts/imports.py pin "$(ID)"; \
	fi; \
	python3 scripts/imports.py mk > imports.mk.tmp; \
	if ! cmp -s imports.mk.tmp imports.mk; then mv imports.mk.tmp imports.mk; else rm imports.mk.tmp; fi; \
	trap - EXIT HUP INT TERM

version-check:
	@set -eu; \
	version='$(CANOE_VERSION)'; \
	version_code='$(CANOE_VERSION_CODE)'; \
	fail=0; \
	if ! python3 scripts/imports.py check; then \
		printf 'version-check: imports.py check failed\n'; \
		fail=1; \
	fi; \
	if ! python3 scripts/imports.py mk | cmp -s - imports.mk; then \
		printf 'version-check: imports.mk is stale or hand-edited\n'; \
		fail=1; \
	fi; \
	fallback='0.0.0-dev'; \
	if [ "$$version" = "$$fallback" ]; then \
		printf 'version invalid: %s is reserved for unstamped-build detection\n' "$$version"; \
		fail=1; \
	fi; \
	if [ "$(CANOE_NONRELEASE)" = "1" ]; then \
		printf 'version-check refused: %s is a non-release override build\n' "$$version"; \
		fail=1; \
	fi; \
	if [ -f README.md ]; then \
		actual="$$(sed -n -E 's/^The ([^ ]+) release surface.*/\1/p' README.md)"; \
	else \
		actual='<missing>'; \
	fi; \
	if [ "$$actual" != "$$version" ]; then \
		printf 'version mismatch: %s expected %s actual %s\n' \
			'README.md release surface' "$$version" "$$actual"; \
		fail=1; \
	fi; \
	if [ -f README_zh.md ]; then \
		actual="$$(sed -n -E 's/^([^ ]+) 的发布界面.*/\1/p' README_zh.md)"; \
	else \
		actual='<missing>'; \
	fi; \
	if [ "$$actual" != "$$version" ]; then \
		printf 'version mismatch: %s expected %s actual %s\n' \
			'README_zh.md 发布界面' "$$version" "$$actual"; \
		fail=1; \
	fi; \
	expected_doc_version="$$version $$version_code"; \
	for doc in wiki/docs/release.md wiki/docs/zh/release.md; do \
		if [ -f "$$doc" ]; then \
			actual="$$(sed -n -E 's/^make bump VERSION=([^ ]+) VERSION_CODE=([^ ]+)$$/\1 \2/p' "$$doc")"; \
		else \
			actual='<missing>'; \
		fi; \
		if [ "$$actual" != "$$expected_doc_version" ]; then \
			printf 'version mismatch: %s release command expected %s actual %s\n' \
				"$$doc" "$$expected_doc_version" "$$actual"; \
			fail=1; \
		fi; \
	done; \
	if [ -f wiki/docs/canoe-cfg.md ]; then \
		actual="$$(sed -n -E 's/.*In ([^ ]+) the boot policy.*/\1/p' wiki/docs/canoe-cfg.md)"; \
	else \
		actual='<missing>'; \
	fi; \
	if [ "$$actual" != "$$version" ]; then \
		printf 'version mismatch: %s expected %s actual %s\n' \
			'wiki/docs/canoe-cfg.md boot policy' "$$version" "$$actual"; \
		fail=1; \
	fi; \
	if [ -f wiki/docs/canoe-cfg.md ]; then \
		actual="$$(sed -n -E 's/^The ([^ ]+) writer manages.*/\1/p' wiki/docs/canoe-cfg.md)"; \
	else \
		actual='<missing>'; \
	fi; \
	if [ "$$actual" != "$$version" ]; then \
		printf 'version mismatch: %s expected %s actual %s\n' \
			'wiki/docs/canoe-cfg.md managed triplets' "$$version" "$$actual"; \
		fail=1; \
	fi; \
	if [ -f wiki/docs/zh/canoe-cfg.md ]; then \
		actual="$$(sed -n -E 's/^[^。]*。([^ ]+) 使用显式启动策略.*/\1/p' wiki/docs/zh/canoe-cfg.md)"; \
	else \
		actual='<missing>'; \
	fi; \
	if [ "$$actual" != "$$version" ]; then \
		printf 'version mismatch: %s expected %s actual %s\n' \
			'wiki/docs/zh/canoe-cfg.md 启动策略' "$$version" "$$actual"; \
		fail=1; \
	fi; \
	if [ -f wiki/docs/zh/canoe-cfg.md ]; then \
		actual="$$(sed -n -E 's/^([^ ]+) 为每个已安装槽位管理一组三件套.*/\1/p' wiki/docs/zh/canoe-cfg.md)"; \
	else \
		actual='<missing>'; \
	fi; \
	if [ "$$actual" != "$$version" ]; then \
		printf 'version mismatch: %s expected %s actual %s\n' \
			'wiki/docs/zh/canoe-cfg.md 受管理三件套' "$$version" "$$actual"; \
		fail=1; \
	fi; \
	stamp='submodules/uefi/edk2/Build/.canoe-version'; \
	if [ -f "$$stamp" ] || [ -f submodules/uefi/build/BDS.efi ]; then \
		if [ -f "$$stamp" ]; then \
			actual="$$(awk 'NR == 1 { value=$$0 } END { if (NR == 1) print value; else print "<invalid>" }' "$$stamp")"; \
		else \
			actual='<missing>'; \
		fi; \
		if [ "$$actual" != "$$version" ]; then \
			printf 'version mismatch: %s expected %s actual %s\n' \
				"$$stamp" "$$version" "$$actual"; \
			fail=1; \
		fi; \
	fi; \
	if [ -f tools/canoe/src/version.rs ]; then \
		actual="$$(awk -F= '$$1 == "pub const VERSION: &str " { value=$$2; gsub(/["; ]/, "", value); print value; found=1 } END { if (!found) print "<missing>" }' tools/canoe/src/version.rs)"; \
	else \
		actual='<missing>'; \
	fi; \
	if [ "$$actual" != "$$version" ]; then \
		printf 'version mismatch: %s expected %s actual %s\n' \
			'tools/canoe/src/version.rs' "$$version" "$$actual"; \
		fail=1; \
	fi; \
	if [ -f targets/magisk_module/module/module.prop ]; then \
		actual="$$(awk -F= '$$1 == "version" { print substr($$0, index($$0, "=") + 1); found=1 } END { if (!found) print "<missing>" }' targets/magisk_module/module/module.prop)"; \
	else \
		actual='<missing>'; \
	fi; \
	if [ "$$actual" != "$$version" ]; then \
		printf 'version mismatch: %s expected %s actual %s\n' \
			'targets/magisk_module/module/module.prop (version)' "$$version" "$$actual"; \
		fail=1; \
	fi; \
	if [ -f targets/magisk_module/module/module.prop ]; then \
		actual="$$(awk -F= '$$1 == "versionCode" { print substr($$0, index($$0, "=") + 1); found=1 } END { if (!found) print "<missing>" }' targets/magisk_module/module/module.prop)"; \
	else \
		actual='<missing>'; \
	fi; \
	if [ "$$actual" != "$$version_code" ]; then \
		printf 'version mismatch: %s expected %s actual %s\n' \
			'targets/magisk_module/module/module.prop (versionCode)' "$$version_code" "$$actual"; \
		fail=1; \
	fi; \
	if [ -f submodules/uefi/build/BDS.efi ]; then \
		if ! command -v strings >/dev/null 2>&1; then \
			printf 'version check unavailable: strings is required to inspect %s\n' \
				'submodules/uefi/build/BDS.efi'; \
			fail=1; \
		else \
			if ! strings -a -e s submodules/uefi/build/BDS.efi | grep -Fq -- "$$version"; then \
				printf 'version mismatch: %s publishes a stale canoe-bds fastboot variable, expected %s (run: make -C submodules/uefi build)\n' \
					'submodules/uefi/build/BDS.efi' "$$version"; \
				fail=1; \
			fi; \
			if ! strings -a -e l submodules/uefi/build/BDS.efi | grep -Fq -- "$$version"; then \
				printf 'version mismatch: %s draws a stale menu credit, expected %s (run: make -C submodules/uefi build)\n' \
					'submodules/uefi/build/BDS.efi' "$$version"; \
				fail=1; \
			fi; \
			if strings -a -e s submodules/uefi/build/BDS.efi | grep -Fqx -- "$$fallback" || \
			   strings -a -e l submodules/uefi/build/BDS.efi | grep -Fqx -- "$$fallback"; then \
				printf 'version mismatch: %s contains the unstamped fallback %s; rebuild with the injected version\n' \
					'submodules/uefi/build/BDS.efi' "$$fallback"; \
				fail=1; \
			fi; \
		fi; \
	fi; \
	if [ -f submodules/uefi/build/BDS.efi ] && command -v sha256sum >/dev/null 2>&1 && command -v unzip >/dev/null 2>&1; then \
		expected="$$(sha256sum submodules/uefi/build/BDS.efi | cut -d" " -f1)"; \
		for archive in targets/toolkit_linux/build/toolkit_linux.zip \
			targets/toolkit_windows/build/toolkit_windows.zip \
			targets/toolkit_android/build/toolkit_android.zip \
			targets/magisk_module/build/module_android.zip; do \
			if [ ! -f "$$archive" ]; then \
				if [ -d "$$(dirname "$$archive")" ]; then \
					printf 'package artifact missing: %s (its build directory exists, so that package build failed rather than never having run)\n' \
						"$$archive"; \
					fail=1; \
				fi; \
				continue; \
			fi; \
			if unzip -l "$$archive" BDS.efi >/dev/null 2>&1; then \
				member="$$(unzip -p "$$archive" BDS.efi | sha256sum | cut -d" " -f1)"; \
			else \
				member='<missing>'; \
			fi; \
			if [ "$$member" != "$$expected" ]; then \
				printf 'package artifact mismatch: %s carries BDS.efi %s expected %s (rebuild that package; a green unit suite does not prove an archive is current)\n' \
					"$$archive" "$$member" "$$expected"; \
				fail=1; \
			fi; \
		done; \
	fi; \
	if [ "$$fail" -ne 0 ]; then exit 1; fi; \
	printf 'Version check passed: %s (version code %s)\n' "$$version" "$$version_code"

target_toolkit_windows:
	cd targets/toolkit_windows && $(MAKE) build
target_toolkit_linux:
	cd targets/toolkit_linux && $(MAKE) build
target_magisk_module:
	cd targets/magisk_module && $(MAKE) build
target_toolkit_android:
	cd targets/toolkit_android && $(MAKE) build

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
	cargo test --locked --manifest-path tools/canoe-bootmgr/Cargo.toml
	cargo test --locked --manifest-path tools/canoe/Cargo.toml
	$(MAKE) -C submodules/patcher test
	$(MAKE) -C submodules/uefi test
	$(MAKE) -C tools/canoe-ext4 test
	sh targets/magisk_module/tests/test_flows.sh
	sh targets/toolkit_android/tests/test_build_script.sh
	python3 -m unittest discover -s scripts/tests -p 'test_*.py'

# Fetch a pinned package asset without ever exposing a partial archive to
# subsequent builds. Package Makefiles invoke this target with absolute
# FETCH_DEST paths so the mechanism is independent of their working directory.
# A file:// URL uses the checked-in last-known-good asset while a release URL
# remains the normal path once the producer repository publishes one.
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
	source_label=Downloaded; \
	case "$(FETCH_URL)" in \
		file://*) source_label=Copied; cp -- "$${FETCH_URL#file://}" "$$tmp" ;; \
		*) wget --no-verbose --tries=3 --output-document="$$tmp" "$(FETCH_URL)" ;; \
	esac; \
	actual="$$(sha256sum "$$tmp" | cut -d" " -f1)"; \
	if [ "$$actual" != "$(FETCH_SHA256)" ]; then \
		echo "sha256 mismatch for $$(basename "$$dest"): expected $(FETCH_SHA256), got $$actual" >&2; \
		exit 1; \
	fi; \
	mv "$$tmp" "$$dest"; \
	trap - EXIT HUP INT TERM; \
	echo "$$source_label and verified $$(basename "$$dest") sha256: $$actual"
