#!/bin/sh
# Focused device-module flow coverage for the 7.x config contract.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/canoe-module-flow.$$
MOD="$TMP/module"
BIN="$TMP/bin"
BY_NAME="$TMP/by-name"
PERSIST="$TMP/persist"
EFISP="$PERSIST/efisp"
LOG="$TMP/flow.log"
trap 'rm -rf "$TMP"' EXIT INT TERM HUP

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "ok - $*"; }
assert_file() { [ -f "$1" ] || fail "missing file: $1"; }
assert_eq() { [ "$1" = "$2" ] || fail "$3 (got '$1', want '$2')"; }
assert_contains() { case "$1" in *"$2"*) ;; *) fail "$3" ;; esac; }

mkdir -p "$MOD/bin" "$MOD/efisp/tools" "$BIN" "$BY_NAME" "$EFISP"
printf 'BDS fixture\n' > "$MOD/BDS.efi"
printf 'tool\n' > "$MOD/efisp/tools/BLTools.efi"
printf 'abl-a-v1\n' > "$BY_NAME/abl_a"
printf 'abl-b-v1\n' > "$BY_NAME/abl_b"
printf 'vbmeta-a\n' > "$BY_NAME/vbmeta_a"
printf 'vbmeta-b\n' > "$BY_NAME/vbmeta_b"
printf 'old-live\n' > "$EFISP/boot.efi"
printf 'old-profile\n' > "$EFISP/boot.efi.gm2p"
printf 'old-tzmap\n' > "$EFISP/boot.efi.tzmap"
printf 'old-backup\n' > "$EFISP/boot_backup.efi"
printf 'old-backup-profile\n' > "$EFISP/boot_backup.efi.gm2p"
printf 'old-backup-tzmap\n' > "$EFISP/boot_backup.efi.tzmap"
: > "$BY_NAME/efisp"

cat > "$BIN/getprop" <<'EOF'
#!/bin/sh
case "$1" in
  ro.boot.slot_suffix) echo _a ;;
  ro.product.name) echo test-device ;;
  ro.product.model) echo Test Model ;;
  ro.board.platform) echo sm8850 ;;
esac
EOF
cat > "$BIN/blockdev" <<'EOF'
#!/bin/sh
case "$1" in
  --getsize64) echo 2097152 ;;
  --getss) echo 512 ;;
  --setrw) exit 0 ;;
  --getro) echo 0 ;;
  *) exit 1 ;;
esac
EOF
cat > "$BIN/grep" <<'EOF'
#!/bin/sh
case "$*" in
  *persist*) exit 0 ;;
  *) exec /usr/bin/grep "$@" ;;
esac
EOF
cat > "$BIN/dd" <<'EOF'
#!/bin/sh
in= out=
for arg in "$@"; do
  case "$arg" in if=*) in=${arg#*=} ;; of=*) out=${arg#*=} ;; esac
done
printf 'dd %s -> %s\n' "$in" "$out" >> "$FLOW_LOG"
if [ -n "$out" ]; then
  [ -f "$in" ] && /bin/cp "$in" "$out"
else
  [ -n "$in" ] && [ -f "$in" ] && /bin/cat "$in"
fi
EOF
cat > "$BIN/sync" <<'EOF'
#!/bin/sh
exit 0
EOF
cat > "$BIN/extractfv" <<'EOF'
#!/bin/sh
out=.
abl=
while [ "$#" -gt 0 ]; do
  case "$1" in -o) out=$2; shift 2 ;; -v) abl=$2; shift 2 ;; *) shift ;; esac
done
printf 'loader-from=' > "$out/LinuxLoader.efi"
cat "$abl" >> "$out/LinuxLoader.efi"
EOF
cat > "$BIN/patch_abl" <<'EOF'
#!/bin/sh
/bin/cp "$1" "$2"
EOF
cat > "$BIN/mode2_profile" <<'EOF'
#!/bin/sh
cmd=$1; shift
case "$cmd" in
  derive)
    vbmeta= out=
    while [ "$#" -gt 0 ]; do
      case "$1" in --vbmeta) vbmeta=$2; shift 2 ;; --out) out=$2; shift 2 ;; *) shift ;; esac
done
    printf 'profile-from=' > "$out"
    cat "$vbmeta" >> "$out"
    ;;
  validate)
    input=
    while [ "$#" -gt 0 ]; do
      case "$1" in --input) input=$2; shift 2 ;; *) shift ;; esac
done
    [ -s "$input" ]
    ;;
  *) exit 1 ;;
esac
EOF
cat > "$BIN/abl_tzmap" <<'EOF'
#!/bin/sh
cmd=$1; shift
case "$cmd" in
  derive)
    abl=$1; shift; out=
    while [ "$#" -gt 0 ]; do
      case "$1" in -o) out=$2; shift 2 ;; *) shift ;; esac
done
    printf 'tzmap-from=' > "$out"
    cat "$abl" >> "$out"
    ;;
  validate)
    [ -s "$1" ]
    ;;
  verify)
    sidecar= abl=
    while [ "$#" -gt 0 ]; do
      case "$1" in --sidecar) sidecar=$2; shift 2 ;; --abl) abl=$2; shift 2 ;; *) shift ;; esac
done
    [ -s "$sidecar" ] && [ -s "$abl" ]
    ;;
  *) exit 1 ;;
esac
EOF
cat > "$BIN/patch_tools" <<'EOF'
#!/bin/sh
printf 'patch_tools %s\n' "$*" >> "$FLOW_LOG"
EOF
chmod +x "$BIN"/*
cp "$BIN/extractfv" "$BIN/patch_abl" "$BIN/mode2_profile" "$BIN/abl_tzmap" "$BIN/patch_tools" "$MOD/bin/"

run() {
  MODDIR="$MOD" BY_NAME_DIR="$BY_NAME" PERSIST_MNT="$PERSIST" EFISP_DIR="$EFISP" \
    FLOW_LOG="$LOG" PATH="$BIN:$PATH" \
    sh "$MOD/bin/bl_flasher.sh" "$@"
}
cp "$ROOT/targets/magisk_module/module/bin/bl_flasher.sh" "$MOD/bin/bl_flasher.sh"
chmod +x "$MOD/bin/bl_flasher.sh"

run flash update-efisp
assert_file "$EFISP/canoe.cfg"
assert_contains "$(cat "$EFISP/canoe.cfg")" 'version 1' 'config version missing'
assert_contains "$(cat "$EFISP/canoe.cfg")" 'entry android-a' 'active entry missing'
assert_contains "$(cat "$EFISP/canoe.cfg")" 'role active' 'active role missing'
assert_contains "$(cat "$EFISP/canoe.cfg")" 'entry android-backup' 'backup entry missing'
assert_contains "$(cat "$EFISP/canoe.cfg")" 'role backup' 'backup role missing'
assert_contains "$(cat "$EFISP/canoe.cfg")" 'image boot_backup.efi' 'backup image missing'
pass 'module pair install writes a valid active and backup canoe.cfg'

printf 'inactive-efi\n' > "$EFISP/boot_b.efi"
printf 'inactive-profile\n' > "$EFISP/boot_b.efi.gm2p"
printf 'inactive-tzmap\n' > "$EFISP/boot_b.efi.tzmap"
run start-mode 2 >/dev/null
sleep 1
assert_contains "$(cat "$EFISP/canoe.cfg")" 'entry android-a' 'mode rewrite removed active entry'
active_block=$(awk '/^entry android-a$/{seen=1} seen{print} /^$/{if(seen) exit}' "$EFISP/canoe.cfg")
assert_contains "$active_block" 'mode 2' 'mode selector did not set the active entry mode'
assert_contains "$(cat "$EFISP/canoe.cfg")" 'entry android-b' 'inactive entry missing'
assert_contains "$(cat "$EFISP/canoe.cfg")" 'role inactive' 'inactive role missing'
pass 'mode selector rewrites the current boot entry, not a partition record'

old_digest=$(sha256sum "$BY_NAME/abl_b" | cut -d ' ' -f1)
printf '%s\n' "$old_digest" > "$EFISP/.canoe.abl_b.sha256"
printf 'abl-b-v2\n' > "$BY_NAME/abl_b"
run_service() {
  MODDIR="$MOD" BY_NAME_DIR="$BY_NAME" EFISP_DIR="$EFISP" \
    PERSIST_MNT="$PERSIST" RUNTIME_DIR="$MOD/tmp" LOG_FILE="$LOG" PATH="$BIN:$PATH" \
    sh "$MOD/service.sh" event "$BY_NAME/abl_b"
}
cp "$ROOT/targets/magisk_module/module/service.sh" "$MOD/service.sh"
chmod +x "$MOD/service.sh"
run_service
new_digest=$(sha256sum "$BY_NAME/abl_b" | cut -d ' ' -f1)
assert_eq "$(cat "$EFISP/.canoe.abl_b.sha256")" "$new_digest" 'watcher did not stamp the changed ABL digest'
assert_file "$EFISP/boot_b.efi"
count_before=$(grep -c 'slot _b changed; derived new boot entry' "$LOG" || true)
run_service
count_after=$(grep -c 'slot _b changed; derived new boot entry' "$LOG" || true)
assert_eq "$count_after" "$count_before" 'unchanged OTA event derived more than once'
assert_contains "$(cat "$EFISP/canoe.cfg")" 'role inactive' 'watcher omitted inactive role'
pass 'OTA watcher derives once and keeps the previous boot entry'


echo 'all module flow fixtures passed'
