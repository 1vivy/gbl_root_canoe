# Single source of truth for Canoe versions.
# `make bump` regenerates every derived file.
# `make version-check` fails when they drift.
# Direct CANOE_VERSION overrides are refused unless an explicit non-release opt-in is present.
_CANOE_VERSION_SOURCE := $(shell sed -n "/^[[:space:]]*CANOE_VERSION[[:space:]]*=/ { s/^[[:space:]]*CANOE_VERSION[[:space:]]*=[[:space:]]*//; p; q; }" $(lastword $(MAKEFILE_LIST)))
CANOE_VERSION = 7.0.0-b7
CANOE_VERSION_CODE = 20
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
