# Single source of truth for Canoe versions.
# `make bump` regenerates every derived file.
# `make version-check` fails when they drift.
# Direct CANOE_VERSION overrides are refused unless an explicit non-release opt-in is present.
_CANOE_VERSION_SOURCE := $(shell sed -n "/^[[:space:]]*CANOE_VERSION[[:space:]]*=/ { s/^[[:space:]]*CANOE_VERSION[[:space:]]*=[[:space:]]*//; p; q; }" $(lastword $(MAKEFILE_LIST)))
CANOE_VERSION = 7.0.0-b2
CANOE_VERSION_CODE = 15
CANOE_VERSION_OVERRIDE ?= 0
ifneq ($(origin CANOE_VERSION),file)
ifneq ($(CANOE_VERSION),$(_CANOE_VERSION_SOURCE))
ifneq ($(CANOE_VERSION_OVERRIDE),1)
$(error CANOE_VERSION override refused: canonical $(_CANOE_VERSION_SOURCE), got $(CANOE_VERSION); set CANOE_VERSION_OVERRIDE=1 only for a non-release build)
endif
ifneq ($(origin CANOE_VERSION_OVERRIDE),command line)
$(error CANOE_VERSION override refused: CANOE_VERSION_OVERRIDE=1 must be supplied on the make command line)
endif
ifneq ($(filter %-local,$(CANOE_VERSION)),)
CANOE_NONRELEASE := 1
else
override CANOE_VERSION := $(CANOE_VERSION)-local
CANOE_NONRELEASE := 1
endif
endif
endif
# Web UI release pin; the archive is a checked-in last-known-good fallback
# until canoe-boot-manager publishes release assets.
CANOE_WEBUI_VERSION = 0.1.0
CANOE_WEBUI_SHA256 = bc8014d5cca35bc28b197fd34c2a136ec246930e9f05bd1ef87dbdd26251a401
CANOE_WEBUI_URL = file://$(CANOE_ROOT_DIR)/targets/magisk_module/webui-cache/canoe-boot-manager-0.1.0.tar.gz
