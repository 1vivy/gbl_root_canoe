# canoe_lib.sh - shared helpers for the canoe host scripts. Sourced, not run.
#
# Used by canoe_prep.sh, canoe_prep_device.sh, canoe_abl.sh and canoe_stage.sh.

die()  { printf '%s: error: %s\n' "${CANOE_PROG:-canoe}" "$1" >&2; exit 1; }
step() { printf '\n[*] %s\n' "$1"; }
warn() { printf '    WARNING: %s\n' "$1" >&2; }

# ---------------------------------------------------------------------- adb --
ADB=adb
ADB_ARGS=()

# dev_init [serial] - resolve adb, wait for the device, prove a shell works.
dev_init() {
  local serial=${1:-}
  if [ -x ./Platform-Tools/adb ]; then
    ADB=./Platform-Tools/adb
  elif ! command -v adb >/dev/null 2>&1; then
    die "adb not found (put it on PATH or in ./Platform-Tools/)"
  fi
  ADB_ARGS=()
  [ -n "$serial" ] && ADB_ARGS=(-s "$serial")
  "$ADB" "${ADB_ARGS[@]}" wait-for-device
  sh_dev true >/dev/null 2>&1 || die "no adb shell (enable ADB in recovery)"
}

sh_dev()   { "$ADB" "${ADB_ARGS[@]}" shell "$@"; }
push_dev() { "$ADB" "${ADB_ARGS[@]}" push "$1" "$2" >/dev/null || die "adb push failed: $1"; }
pull_dev() { "$ADB" "${ADB_ARGS[@]}" pull "$1" "$2" >/dev/null || die "adb pull failed: $1"; }
dev_size() { sh_dev "wc -c < $1" | tr -d '\r\n '; }

# --------------------------------------------------------------------- slot --
# detect_slot - echo the active slot suffix (_a / _b), or empty for non-A/B.
# Never guesses: an undetectable slot is an error the caller must resolve with
# an explicit --slot, because writing the wrong slot is a boot risk.
detect_slot() {
  local s
  s=$(sh_dev "getprop ro.boot.slot_suffix" 2>/dev/null | tr -d '\r\n ')
  if [ -z "$s" ]; then
    s=$(sh_dev "cat /proc/cmdline" 2>/dev/null | tr ' ' '\n' \
        | sed -n 's/^androidboot\.slot_suffix=//p' | tr -d '\r\n ')
  fi
  printf '%s' "$s"
}

# resolve_part <base> <slot> - echo the by-name path for <base>.
#
# When <slot> is non-empty the slotted name is REQUIRED: falling back to the bare
# name would silently read or write a different partition than the caller asked
# for. The bare name is used only when the caller passed no slot at all, which is
# either a non-A/B layout or a partition that is never slotted (efisp).
resolve_part() {
  local base=$1 slot=$2 p
  if [ -n "$slot" ]; then
    p="/dev/block/by-name/${base}${slot}"
    if sh_dev "[ -e $p ]"; then printf '%s' "$p"; return 0; fi
    return 1
  fi
  p="/dev/block/by-name/${base}"
  if sh_dev "[ -e $p ]"; then printf '%s' "$p"; return 0; fi
  return 1
}

# ------------------------------------------------------------------- reads --
# dump_part <block_dev> <local_out> - copy a whole partition to the host.
dump_part() {
  local dev=$1 out=$2 tmp=/tmp/canoe-dump.img
  sh_dev "dd if=$dev of=$tmp bs=4M" >/dev/null 2>&1 \
    || die "could not read $dev"
  pull_dev "$tmp" "$out"
  sh_dev "rm -f $tmp" >/dev/null 2>&1 || true
  [ -s "$out" ] || die "dump of $dev is empty"
}

# verified_write <local_img> <block_dev> <local_backup>
#
# Backs the partition up to the host first, writes, then compares the written
# region byte-for-byte against the source. Any failure restores the partition
# from the on-device copy of the backup before returning non-zero. Only the
# leading len(local_img) bytes are written, so anything the image does not cover
# (for efisp: the mode/default/custom records in the last MiB) is left alone.
verified_write() {
  local img=$1 dev=$2 backup=$3
  local img_bytes part_bytes rtmp="/tmp/canoe-w.img" btmp="/tmp/canoe-w.bak"

  [ -s "$img" ] || die "image is missing or empty: $img"
  img_bytes=$(wc -c < "$img")
  part_bytes=$(sh_dev "blockdev --getsize64 $dev" | tr -d '\r\n ')
  case "$part_bytes" in
    ''|*[!0-9]*) die "could not read the size of $dev" ;;
  esac
  printf '    %s: %s bytes; image: %s bytes\n' "$dev" "$part_bytes" "$img_bytes"
  [ "$img_bytes" -le "$part_bytes" ] || die "image does not fit in $dev"

  sh_dev "dd if=$dev of=$btmp bs=4M" >/dev/null 2>&1 \
    || die "could not back up $dev"
  pull_dev "$btmp" "$backup"
  [ "$(wc -c < "$backup")" = "$part_bytes" ] \
    || die "backup of $dev is the wrong size; refusing to write"
  printf '    backed up to %s\n' "$backup"

  push_dev "$img" "$rtmp"
  sh_dev "blockdev --setrw $dev" || die "blockdev --setrw failed on $dev"

  _vw_restore() {
    warn "restoring $dev from the on-device backup"
    sh_dev "dd if=$btmp of=$dev bs=4M conv=fsync && sync" \
      || warn "restore failed; reflash $dev from $backup before rebooting"
  }

  if ! sh_dev "dd if=$rtmp of=$dev bs=4M conv=fsync && sync"; then
    _vw_restore
    sh_dev "rm -f $rtmp $btmp" >/dev/null 2>&1 || true
    die "write to $dev failed"
  fi

  # Compare the full written length, not a magic prefix.
  if ! sh_dev "dd if=$dev of=$rtmp.rb bs=$img_bytes count=1" >/dev/null 2>&1 \
     || ! sh_dev "cmp $rtmp $rtmp.rb"; then
    _vw_restore
    sh_dev "rm -f $rtmp $rtmp.rb $btmp" >/dev/null 2>&1 || true
    die "verification of $dev failed"
  fi
  printf '    verified %s bytes byte-for-byte\n' "$img_bytes"
  sh_dev "rm -f $rtmp $rtmp.rb $btmp" >/dev/null 2>&1 || true
}

# ----------------------------------------------------------------- persist ---
# find_persist [preferred] - echo the persist mount point, mounting it if needed.
find_persist() {
  local want=${1:-} cand
  if [ -n "$want" ]; then
    sh_dev "grep -q ' $want ' /proc/mounts" || die "persist is not mounted at $want"
    printf '%s' "$want"; return 0
  fi
  for cand in /persist /mnt/vendor/persist; do
    if sh_dev "grep -q ' $cand ' /proc/mounts" 2>/dev/null; then
      printf '%s' "$cand"; return 0
    fi
  done
  sh_dev "mkdir -p /persist && mount -t ext4 /dev/block/by-name/persist /persist" \
    >/dev/null 2>&1 || die "could not mount persist; pass --persist PATH"
  sh_dev "grep -q ' /persist ' /proc/mounts" || die "persist did not mount"
  printf '%s' /persist
}
