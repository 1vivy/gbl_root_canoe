# Single source of truth for Canoe versions.
# `make bump` regenerates every derived file.
# `make version-check` fails when they drift.
CANOE_VERSION = 7.0.0-b2
CANOE_VERSION_CODE = 15
# Web UI release pin; the archive is a checked-in last-known-good fallback
# until canoe-boot-manager publishes release assets.
CANOE_WEBUI_VERSION = 0.1.0
CANOE_WEBUI_SHA256 = ebc631e5fa91f0011bcdf7fcf5afa512aa50db0881a210b93b9bb326550445a0
CANOE_WEBUI_URL = file://$(CANOE_ROOT_DIR)/targets/magisk_module/webui-cache/canoe-boot-manager-0.1.0.tar.gz
