#!/bin/sh
# The module installer is a bootstrap; boot-chain work is covered by the
# canoe-manager and app suites, not by a second shell implementation.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/canoe-module-bootstrap.$$
MOD="$TMP/module"
BY_NAME="$TMP/by-name"
BOOT_ROOT="$TMP/persist/efisp"
ABL_REPO="$MOD/ablrepo/test-device"
MARKER="$TMP/partition-operation.marker"
UI_LOG="$TMP/ui.log"
CONFIG_LOG="$TMP/config.log"
trap 'rm -rf "$TMP"' EXIT INT TERM HUP

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "ok - $*"; }
assert_file() { [ -f "$1" ] || fail "missing file: $1"; }
assert_eq() { [ "$1" = "$2" ] || fail "$3 (got '$1', want '$2')"; }
assert_contains() { case "$1" in *"$2"*) ;; *) fail "$3" ;; esac; }

mkdir -p "$MOD/bin" "$MOD/webroot" "$MOD/efisp/tools" "$BY_NAME" \
  "$BOOT_ROOT/tools" "$ABL_REPO"
cp "$ROOT/targets/magisk_module/module/customize.sh" "$MOD/customize.sh"
# This suite covers package-only setup; native deployment is exercised through the guest installer.
printf 'canoe_install_flow() { :; }\n' > "$MOD/install-flow.sh"
printf 'abl-a-before\n' > "$BY_NAME/abl_a"
printf 'abl-b-before\n' > "$BY_NAME/abl_b"
printf 'efisp-before\n' > "$BY_NAME/efisp"
printf 'old-config\n' > "$BOOT_ROOT/canoe.cfg"
printf 'old-bds\n' > "$BOOT_ROOT/BDS.efi"
printf 'old-tool\n' > "$BOOT_ROOT/tools/BLTools.efi"
printf 'old-sidecar\n' > "$BOOT_ROOT/boot_a.efi"
printf 'old-profile\n' > "$BOOT_ROOT/boot_a.efi.gm2p"
printf 'old-tzmap\n' > "$BOOT_ROOT/boot_a.efi.tzmap"
printf 'zh\n' > "$MOD/lang.txt"
printf 'abl fixture\n' > "$ABL_REPO/abl.img"
printf 'digest fixture\n' > "$ABL_REPO/abl.sha256"
printf 'metadata fixture\n' > "$ABL_REPO/abl.meta"

before_root="$TMP/root.before.tar"
after_root="$TMP/root.after.tar"
tar -cf "$before_root" -C "$BOOT_ROOT" .
cp "$BY_NAME/abl_a" "$TMP/abl_a.before"
cp "$BY_NAME/abl_b" "$TMP/abl_b.before"
cp "$BY_NAME/efisp" "$TMP/efisp.before"

cat > "$TMP/getprop" <<'EOF'
#!/bin/sh
case "$1" in
  ro.product.model) printf 'Test-Model\n' ;;
  ro.product.name) printf 'test-device\n' ;;
  ro.build.version.incremental) printf 'test-build\n' ;;
  persist.sys.locale) printf '%s\n' "${SYS_LOCALE-}" ;;
  ro.product.locale) printf '%s\n' "${PRODUCT_LOCALE-}" ;;
esac
EOF
cat > "$TMP/ksud" <<'EOF'
#!/bin/sh
if [ "$1" = module ] && [ "$2" = config ] && [ "$3" = set ]; then
  printf '%s=%s\n' "$4" "$5" >> "${CONFIG_LOG:?}"
  exit 0
fi
if [ "$1" = module ] && [ "$2" = config ] && [ "$3" = get ]; then
  printf '%s\n' "${KSUD_LANG-en}"
  exit 0
fi
exit 1
EOF
cat > "$TMP/dd" <<'EOF'
#!/bin/sh
printf 'dd invoked\n' >> "${MARKER:?}"
exit 99
EOF
cat > "$TMP/blockdev" <<'EOF'
#!/bin/sh
printf 'blockdev invoked\n' >> "${MARKER:?}"
exit 99
EOF
cat > "$TMP/reboot" <<'EOF'
#!/bin/sh
printf 'reboot invoked\n' >> "${MARKER:?}"
exit 99
EOF
cat > "$TMP/canoe-manager" <<'EOF'
#!/bin/sh
printf 'canoe-manager invoked\n' >> "${MARKER:?}"
exit 99
EOF
chmod +x "$TMP/getprop" "$TMP/ksud" "$TMP/dd" "$TMP/blockdev" \
  "$TMP/reboot" "$TMP/canoe-manager"

cat > "$TMP/bootstrap-wrapper.sh" <<'EOF'
#!/bin/sh
ui_print() { printf '%s\n' "$*" >> "${UI_LOG:?}"; }
abort() { printf 'ABORT: %s\n' "$*" >> "${UI_LOG:?}"; exit 1; }
set_perm_recursive() { :; }
set_perm() { :; }
. "$MODPATH/customize.sh"
EOF
chmod +x "$TMP/bootstrap-wrapper.sh"

MODPATH="$MOD" UI_LOG="$UI_LOG" CONFIG_LOG="$CONFIG_LOG" \
  MARKER="$MARKER" BY_NAME_DIR="$BY_NAME" PATH="$TMP:$PATH" \
  sh "$TMP/bootstrap-wrapper.sh"

assert_file "$MOD/lang.txt"
assert_eq "$(tr -d '[:space:]' < "$MOD/lang.txt")" zh \
  'the stored language selection was not retained'
assert_contains "$(cat "$CONFIG_LOG")" 'user_lang=zh' \
  'the language selection was not exposed to the module setting'
assert_contains "$(cat "$UI_LOG")" 'Test-Model / test-device / test-build' \
  'device facts were not displayed'
[ ! -e "$MARKER" ] || fail 'bootstrap invoked a partition operation'
rm -f "$MOD/lang.txt"
SYS_LOCALE=zh-CN PRODUCT_LOCALE= KSUD_LANG= MODPATH="$MOD" \
  UI_LOG="$UI_LOG" CONFIG_LOG="$CONFIG_LOG" MARKER="$MARKER" \
  BY_NAME_DIR="$BY_NAME" PATH="$TMP:$PATH" sh "$TMP/bootstrap-wrapper.sh"
assert_eq "$(tr -d '[:space:]' < "$MOD/lang.txt")" zh \
  'a Chinese device locale was not persisted'

rm -f "$MOD/lang.txt"
SYS_LOCALE= PRODUCT_LOCALE=en-US KSUD_LANG= MODPATH="$MOD" \
  UI_LOG="$UI_LOG" CONFIG_LOG="$CONFIG_LOG" MARKER="$MARKER" \
  BY_NAME_DIR="$BY_NAME" PATH="$TMP:$PATH" sh "$TMP/bootstrap-wrapper.sh"
assert_eq "$(tr -d '[:space:]' < "$MOD/lang.txt")" en \
  'a non-Chinese device locale was not persisted'

printf 'zh\n' > "$MOD/lang.txt"
SYS_LOCALE=en-US PRODUCT_LOCALE=zh-CN KSUD_LANG= MODPATH="$MOD" \
  UI_LOG="$UI_LOG" CONFIG_LOG="$CONFIG_LOG" MARKER="$MARKER" \
  BY_NAME_DIR="$BY_NAME" PATH="$TMP:$PATH" sh "$TMP/bootstrap-wrapper.sh"
assert_eq "$(tr -d '[:space:]' < "$MOD/lang.txt")" zh \
  'an existing language selection was overwritten on update'


cmp "$TMP/abl_a.before" "$BY_NAME/abl_a" || fail 'active ABL changed'
cmp "$TMP/abl_b.before" "$BY_NAME/abl_b" || fail 'inactive ABL changed'
cmp "$TMP/efisp.before" "$BY_NAME/efisp" || fail 'efisp partition changed'
tar -cf "$after_root" -C "$BOOT_ROOT" .
cmp "$before_root" "$after_root" || fail 'boot root changed during bootstrap'
[ -d "$MOD/ablrepo" ] || fail 'bundled ABL repository was not retained for offline lookup'
assert_file "$ABL_REPO/abl.img"
pass 'bootstrap writes no partition and leaves the boot root byte-identical'

sh -n "$ROOT/targets/magisk_module/module/customize.sh"
assert_contains "$(cat "$UI_LOG")" 'Installing this module alone does not require formatting data.' \
  'module installation was not distinguished from boot-chain deployment'
assert_contains "$(cat "$UI_LOG")" 'Reboot when KernelSU requests module activation' \
  'activation guidance was not displayed'
assert_contains "$(cat "$UI_LOG")" '仅安装管理界面和工具时不会更改启动分区' \
  'Chinese activation guidance is absent'
pass 'bootstrap explains module activation separately from boot-chain deployment'
echo 'all module bootstrap fixtures passed'
