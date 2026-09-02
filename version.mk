# Single source of truth for Canoe versions.
# `make bump` regenerates every derived file.
# `make version-check` fails when they drift.
CANOE_VERSION = 7.0.0-b2
CANOE_VERSION_CODE = 15
# Web UI release pin; the archive is a checked-in last-known-good fallback
# until canoe-boot-manager publishes release assets.
CANOE_WEBUI_VERSION = 0.1.0
CANOE_WEBUI_SHA256 = e294a02d27bfd6494f873732e6fc55ef6b1566697b1860144136e781e06c25ff
CANOE_WEBUI_URL = file://$(CANOE_ROOT_DIR)/targets/magisk_module/webui-cache/canoe-boot-manager-0.1.0.tar.gz
