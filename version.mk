# Single source of truth for Canoe versions.
# `make bump` regenerates every derived file.
# `make version-check` fails when they drift.
CANOE_VERSION = 7.0.0-b2
CANOE_VERSION_CODE = 15
# Web UI release pin; the archive is a checked-in last-known-good fallback
# until canoe-boot-manager publishes release assets.
CANOE_WEBUI_VERSION = 0.1.0
CANOE_WEBUI_SHA256 = 43eebeece08ef64ffb71fb1a7236b1f89970b97407bc692f560b477e9f472af2
CANOE_WEBUI_URL = file://$(CANOE_ROOT_DIR)/targets/magisk_module/webui-cache/canoe-boot-manager-0.1.0.tar.gz
