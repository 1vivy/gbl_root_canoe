#!/system/bin/sh
set -u

MODDIR=${MODDIR:-$(CDPATH= cd -- "$(dirname "$0")/.." 2>/dev/null && pwd)}
PERSIST_MNT=${PERSIST_MNT:-/mnt/vendor/persist}
SOURCE_EFISP=${SOURCE_EFISP:-$PERSIST_MNT/efisp}
FAT_FILE=${FAT_FILE:-$PERSIST_MNT/efisp.fat}
PERSIST_DEVICE=${PERSIST_DEVICE:-/dev/block/by-name/persist}
FAT_SIZE=${FAT_SIZE:-16777216}
FIEMAP_HELPER=${FIEMAP_HELPER:-$MODDIR/bin/fiemap}
EXTENT_SOURCE=${EXTENT_SOURCE:-}
RUNTIME_DIR=${RUNTIME_DIR:-$MODDIR/tmp}
RUNS_FILE=$RUNTIME_DIR/fat-runs.$$
RUNLIST_FILE=$RUNTIME_DIR/fat-runlist.$$
TRAILER_FILE=$RUNTIME_DIR/fat-trailer.$$
BLOCKSIZE_FILE=$RUNTIME_DIR/fat-blocksize.$$

LOOP_NODE=
MOUNT_POINT=
MOUNTED=0
REMOVE_ON_FAIL=0
RUN_SOURCE=none
RUN_BLOCKSIZE=4096
RUN_COUNT=0
RUNS=

cleanup() {
  status=$?
  if [ "$MOUNTED" -eq 1 ]; then umount "$MOUNT_POINT" >/dev/null 2>&1 || :; fi
  if [ -n "$LOOP_NODE" ]; then losetup -d "$LOOP_NODE" >/dev/null 2>&1 || :; fi
  if [ "$REMOVE_ON_FAIL" -eq 1 ]; then rm -f "$FAT_FILE"; fi
  rm -f "$RUNS_FILE" "$RUNLIST_FILE" "$TRAILER_FILE" "$BLOCKSIZE_FILE" \
    "$RUNTIME_DIR/fat-extents.$$" "$RUNTIME_DIR/fat-error.$$" "$RUNTIME_DIR/fat-coalesced.$$"
  [ -z "$MOUNT_POINT" ] || rmdir "$MOUNT_POINT" >/dev/null 2>&1 || :
  exit "$status"
}
trap cleanup 0
trap 'exit 1' INT TERM HUP

say() { printf '%s\n' "$*"; }
fail() { say "ERROR=$*" >&2; exit 1; }
num() { case "$1" in ''|*[!0-9]*) return 1 ;; esac; }
size_bytes() {
  case "$1" in
    *K|*k) value=${1%[Kk]}; num "$value" || return 1; echo $((value * 1024)) ;;
    *M|*m) value=${1%[Mm]}; num "$value" || return 1; echo $((value * 1048576)) ;;
    *G|*g) value=${1%[Gg]}; num "$value" || return 1; echo $((value * 1073741824)) ;;
    *) num "$1" || return 1; echo "$1" ;;
  esac
}
file_size() {
  if command -v stat >/dev/null 2>&1; then stat -c %s "$1" 2>/dev/null && return; fi
  wc -c < "$1" | awk '{print $1}'
}
file_blocks() { stat -c %b "$1" 2>/dev/null || return 1; }
free_bytes() {
  if [ -n "${PERSIST_FREE_BYTES:-}" ]; then printf '%s\n' "$PERSIST_FREE_BYTES"; return; fi
  df -Pk "$PERSIST_MNT" 2>/dev/null | awk 'NR == 2 { print $4 * 1024; exit }'
}
check_nonsparse() {
  size=$(file_size "$FAT_FILE") || return 1
  blocks=$(file_blocks "$FAT_FILE") || return 1
  num "$size" && num "$blocks" || return 1
  expected_blocks=$(( (size + 511) / 512 ))
  [ "$blocks" -ge "$expected_blocks" ]
}
fat16_minimum_sectors() {
  bytes_per_sector=512
  sectors_per_cluster=1
  min_clusters=4085
  fat_count=2
  reserved_sectors=1
  root_entries=512
  root_sectors=$(( (root_entries * 32 + bytes_per_sector - 1) / bytes_per_sector ))
  fat_sectors=$(( ((min_clusters + 2) * 2 + bytes_per_sector - 1) / bytes_per_sector ))
  printf '%s\n' $((reserved_sectors + fat_count * fat_sectors + root_sectors + min_clusters * sectors_per_cluster))
}

le_escape() {
  value=$1; bytes=$2; escape=
  while [ "$bytes" -gt 0 ]; do
    escape=$escape\\$(printf '%03o' $((value % 256)))
    value=$((value / 256)); bytes=$((bytes - 1))
  done
  printf '%s' "$escape"
}
write_le() { escape=$(le_escape "$1" "$2") || return 1; printf '%b' "$escape"; }
hex_bytes() {
  hex=$1; pos=1; out=
  while [ "$pos" -le "${#hex}" ]; do
    pair=$(printf '%s' "$hex" | cut -c "$pos-$((pos + 1))")
    value=$(printf '%d' "0x$pair") || return 1
    out=$out\\$(printf '%03o' "$value"); pos=$((pos + 2))
  done
  printf '%b' "$out"
}
le_hex() {
  value=$1; bytes=$2; out=
  while [ "$bytes" -gt 0 ]; do
    out=$out$(printf '%02x' $((value % 256)))
    value=$((value / 256)); bytes=$((bytes - 1))
  done
  printf '%s' "$out"
}
read_hex() { dd if="$FAT_FILE" bs=1 skip="$1" count="$2" 2>/dev/null | od -An -tx1 | tr -d ' \n'; }

parse_extents() {
  awk -v bsfile="$BLOCKSIZE_FILE" '
    function clean(value) { gsub(/[ \t]/, "", value); return value }
    function range_first(value, first) { first = value; sub(/\.\..*/, "", first); return first }
    function range_last(value, last) { last = value; sub(/^.*\.\./, "", last); return last }
    # Every field arrives as a string, so each comparison against a running
    # position is forced numeric with +0. Without that, an uninitialised
    # logical position ("") compares stringwise against "0" and the first
    # extent line of a real filefrag/debugfs dump is rejected outright.
    BEGIN { logical = 0; next_logical = 0 }
    /^[ \t]*blocksize:[0-9]+[ \t]*$/ { blocksize = $0; sub(/^[^:]*:/, "", blocksize); next }
    /^[ \t]*Filesystem blocksize:/ {
      for (i = 1; i <= NF; i++) if ($i ~ /^[0-9]+$/) blocksize = $i
      next
    }
    /^[ \t]*[0-9]+:[0-9]+[ \t]*$/ {
      split($0, fields, ":")
      physical = clean(fields[1]); count = clean(fields[2])
      if (physical !~ /^[0-9]+$/ || count !~ /^[0-9]+$/ || count == 0 || physical < 0) exit 2
      if (logical != 0 && logical != next_logical) exit 2
      print physical ":" count; next_logical += count; logical = next_logical; next
    }
    /^[ \t]*[0-9]+:/ {
      count = split($0, fields, ":")
      if (count < 3) next
      logical_range = clean(fields[2]); physical_range = clean(fields[3])
      first_logical = range_first(logical_range); last_logical = range_last(logical_range)
      first_physical = range_first(physical_range); last_physical = range_last(physical_range)
      if (first_logical !~ /^[0-9]+$/ || last_logical !~ /^[0-9]+$/ ||
          first_physical !~ /^[0-9]+$/ || last_physical !~ /^[0-9]+$/ ||
          last_logical + 0 < first_logical + 0 ||
          last_physical + 0 < first_physical + 0 ||
          logical + 0 != first_logical + 0) exit 2
      print first_physical ":" (last_physical - first_physical + 1)
      logical = last_logical + 1; next_logical = logical + 0; next
    }
    END {
      if (blocksize !~ /^[0-9]+$/ || blocksize == 0) blocksize = 4096
      print blocksize > bsfile
    }
  '
}
coalesce_runs() {
  awk -F: '
    NF == 2 && $1 ~ /^[0-9]+$/ && $2 ~ /^[0-9]+$/ && $2 > 0 {
      if (have && physical + count == $1) count += $2
      else { if (have) print physical ":" count; physical = $1; count = $2; have = 1 }
    }
    END { if (have) print physical ":" count }
  '
}
resolve_runs() {
  source=${EXTENT_SOURCE:-}; raw=$RUNTIME_DIR/fat-extents.$$; error=$RUNTIME_DIR/fat-error.$$
  mkdir -p "$RUNTIME_DIR" || return 1
  : > "$raw" || return 1
  : > "$error" || return 1
  if [ -z "$source" ] && [ -x "$FIEMAP_HELPER" ]; then source=fiemap; fi
  if [ -z "$source" ] && command -v filefrag >/dev/null 2>&1; then source=filefrag; fi
  if [ -z "$source" ] && command -v debugfs >/dev/null 2>&1; then source=debugfs; fi
  case "$source" in
    fiemap)
      [ -x "$FIEMAP_HELPER" ] || { say "ERROR=fiemap helper missing: $FIEMAP_HELPER" >&2; return 1; }
      "$FIEMAP_HELPER" "$FAT_FILE" > "$raw" 2> "$error" || { cat "$error" >&2; return 1; } ;;
    filefrag) command -v filefrag >/dev/null 2>&1 || { say 'ERROR=extent source unavailable: filefrag' >&2; return 1; }; filefrag -v "$FAT_FILE" > "$raw" 2> "$error" || { cat "$error" >&2; return 1; } ;;
    debugfs) command -v debugfs >/dev/null 2>&1 || { say 'ERROR=extent source unavailable: debugfs' >&2; return 1; }; debugfs -R "filefrag -v $FAT_FILE" "$PERSIST_DEVICE" > "$raw" 2> "$error" || { cat "$error" >&2; return 1; } ;;
    *) say 'ERROR=fiemap helper missing and filefrag/debugfs unavailable' >&2; return 1 ;;
  esac
  parse_extents < "$raw" > "$RUNS_FILE" || { rm -f "$raw" "$error"; return 1; }
  RUN_BLOCKSIZE=$(cat "$BLOCKSIZE_FILE") || return 1
  num "$RUN_BLOCKSIZE" || return 1
  [ "$RUN_BLOCKSIZE" -gt 0 ] || return 1
  coalesced=$RUNTIME_DIR/fat-coalesced.$$
  coalesce_runs < "$RUNS_FILE" > "$coalesced" || return 1
  mv "$coalesced" "$RUNS_FILE" || return 1
  actual_size=$(file_size "$FAT_FILE") || return 1
  needed=$(( actual_size / RUN_BLOCKSIZE )); [ $((actual_size % RUN_BLOCKSIZE)) -eq 0 ] || needed=$((needed + 1))
  covered=$(awk -F: '{ total += $2 } END { print total + 0 }' "$RUNS_FILE")
  [ "$covered" -ge "$needed" ] || { say 'ERROR=resolved extents do not cover every file block' >&2; return 1; }
  RUN_SOURCE=$source; RUN_COUNT=$(wc -l < "$RUNS_FILE" | awk '{print $1}'); RUNS=$(tr '\n' ',' < "$RUNS_FILE" | sed 's/,$//')
  rm -f "$raw" "$error"
}
make_runlist() {
  mkdir -p "$RUNTIME_DIR" || return 1
  : > "$RUNLIST_FILE" || return 1
  while IFS=: read -r physical count; do
    num "$physical" && num "$count" && [ "$count" -gt 0 ] || return 1
    write_le "$physical" 8 >> "$RUNLIST_FILE" || return 1
    write_le "$count" 8 >> "$RUNLIST_FILE" || return 1
  done < "$RUNS_FILE"
}
sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'; return; fi
  if command -v toybox >/dev/null 2>&1; then toybox sha256sum "$1" | awk '{print $1}'; return; fi
  return 1
}
write_trailer() {
  size=$1; volume=$((size - 4096)); hash=$(sha256_file "$RUNLIST_FILE") || return 1
  dd if=/dev/zero of="$TRAILER_FILE" bs=4096 count=1 conv=fsync 2>/dev/null || return 1
  printf 'CANOEFT1' | dd of="$TRAILER_FILE" bs=1 seek=0 conv=notrunc 2>/dev/null || return 1
  write_le "$size" 8 | dd of="$TRAILER_FILE" bs=1 seek=8 conv=notrunc 2>/dev/null || return 1
  write_le "$volume" 8 | dd of="$TRAILER_FILE" bs=1 seek=16 conv=notrunc 2>/dev/null || return 1
  write_le "$RUN_COUNT" 4 | dd of="$TRAILER_FILE" bs=1 seek=24 conv=notrunc 2>/dev/null || return 1
  write_le 0 4 | dd of="$TRAILER_FILE" bs=1 seek=28 conv=notrunc 2>/dev/null || return 1
  hex_bytes "$hash" | dd of="$TRAILER_FILE" bs=1 seek=32 conv=notrunc 2>/dev/null || return 1
  dd if="$TRAILER_FILE" of="$FAT_FILE" bs=4096 seek=$((volume / 4096)) conv=notrunc 2>/dev/null
}
stamp_and_report() {
  size=$(file_size "$FAT_FILE") || return 1
  resolve_runs || return 1
  make_runlist || return 1
  write_trailer "$size" || return 1
  verify_file "$size"
}
verify_file() {
  expected=$(size_bytes "${1:-$FAT_SIZE}") || { say 'ERROR=invalid expected size' >&2; return 1; }
  exists=0; nonsparse=0; size_ok=0; stamp=0; boundary=0; trailer_length=0
  source=none; actual_size=0; RUN_COUNT=0; RUNS=; volume=0
  [ -f "$FAT_FILE" ] && exists=1
  if [ "$exists" -eq 1 ]; then
    actual_size=$(file_size "$FAT_FILE") || actual_size=0
    check_nonsparse && nonsparse=1 || :
    [ "$actual_size" = "$expected" ] && size_ok=1 || :
    if [ "$actual_size" -ge 4096 ] && [ "$nonsparse" -eq 1 ] && resolve_runs; then
      source=$RUN_SOURCE; make_runlist || :
      hash=$(sha256_file "$RUNLIST_FILE" 2>/dev/null || printf '')
      base=$((actual_size - 4096)); volume=$base
      magic=$(read_hex "$base" 8); stored_size=$(read_hex $((base + 8)) 8)
      stored_volume=$(read_hex $((base + 16)) 8); stored_count=$(read_hex $((base + 24)) 4)
      stored_hash=$(read_hex $((base + 32)) 32)
      [ "$stored_volume" = "$(le_hex "$base" 8)" ] && boundary=1 || :
      [ "$magic" = 43414e4f45465431 ] && [ "$stored_size" = "$(le_hex "$actual_size" 8)" ] && trailer_length=1 || :
      [ "$boundary" -eq 1 ] && [ "$trailer_length" -eq 1 ] && [ "$stored_count" = "$(le_hex "$RUN_COUNT" 4)" ] && [ "$stored_hash" = "$hash" ] && stamp=1 || :
    fi
  fi
  say "EXISTS=$exists|SIZE=$actual_size|NON_SPARSE=$nonsparse|SIZE_OK=$size_ok|RUN_COUNT=$RUN_COUNT|RUNS=${RUNS:-}|EXTENT_SOURCE=$source|STAMP_MATCH=$stamp|STAMP_VALID=$stamp|FAT_VOLUME_BYTES=$volume|FAT_BOUNDARY_MATCH=$boundary|TRAILER_LENGTH_MATCH=$trailer_length"
  [ "$exists" -eq 1 ] && [ "$nonsparse" -eq 1 ] && [ "$size_ok" -eq 1 ] && [ "$boundary" -eq 1 ] && [ "$trailer_length" -eq 1 ] && [ "$stamp" -eq 1 ]
}

copy_tree() {
  [ -f "$SOURCE_EFISP/BOOTENTRIES" ] || fail "missing source file: BOOTENTRIES"
  [ -f "$SOURCE_EFISP/boot.efi" ] || fail "missing source file: boot.efi"
  [ -f "$SOURCE_EFISP/boot.efi.gm2p" ] || fail "missing source file: boot.efi.gm2p"
  [ -f "$SOURCE_EFISP/boot.efi.tzmap" ] || fail "missing source file: boot.efi.tzmap"
  [ -d "$SOURCE_EFISP/tools" ] || fail 'missing source directory: tools'
  cp -p "$SOURCE_EFISP/BOOTENTRIES" "$MOUNT_POINT/BOOTENTRIES" || fail 'copy BOOTENTRIES failed'
  cp -p "$SOURCE_EFISP/boot.efi" "$MOUNT_POINT/boot.efi" || fail 'copy boot.efi failed'
  cp -p "$SOURCE_EFISP/boot.efi.gm2p" "$MOUNT_POINT/boot.efi.gm2p" || fail 'copy boot.efi.gm2p failed'
  cp -p "$SOURCE_EFISP/boot.efi.tzmap" "$MOUNT_POINT/boot.efi.tzmap" || fail 'copy boot.efi.tzmap failed'
  for backup in boot_backup.efi boot_backup.efi.gm2p boot_backup.efi.tzmap; do
    [ -e "$SOURCE_EFISP/$backup" ] || continue
    cp -p "$SOURCE_EFISP/$backup" "$MOUNT_POINT/$backup" || fail "copy $backup failed"
  done
  cp -pr "$SOURCE_EFISP/tools" "$MOUNT_POINT/tools" || fail 'copy tools failed'
}
provision() {
  [ "$#" -le 1 ] || fail 'usage: provision [size]'
  requested=$(size_bytes "${1:-$FAT_SIZE}") || fail 'invalid size'
  num "$requested" || fail 'invalid size'
  [ $((requested % 4096)) -eq 0 ] || fail 'size must be a 4096-byte multiple'
  volume_sectors=$(( (requested - 4096) / 512 ))
  min_sectors=$(fat16_minimum_sectors)
  [ "$volume_sectors" -ge "$min_sectors" ] || fail "size below FAT16 minimum (${min_sectors} sectors for 4085 clusters)"
  mkdir -p "$PERSIST_MNT" "$RUNTIME_DIR" || fail 'persist mount unavailable'
  [ ! -e "$FAT_FILE" ] || fail "file already exists: $FAT_FILE"
  free=$(free_bytes); num "$free" || fail 'persist free space unavailable'
  [ "$free" -ge "$requested" ] || fail 'insufficient persist free space'
  [ -f "$SOURCE_EFISP/BOOTENTRIES" ] || fail 'missing source efisp tree'
  MOUNT_POINT=$RUNTIME_DIR/fat-mnt.$$
  mkdir -p "$MOUNT_POINT" || fail 'mount point unavailable'
  REMOVE_ON_FAIL=1
  dd if=/dev/zero of="$FAT_FILE" bs=4096 count=$((requested / 4096)) conv=fsync 2>/dev/null || fail 'zero fill failed'
  filled=$(file_size "$FAT_FILE") || fail 'filled file size unavailable'
  [ "$filled" = "$requested" ] || fail 'zero fill size mismatch'
  check_nonsparse || fail 'file allocation is short of its length'
  LOOP_NODE=$(losetup -f 2>/dev/null) || fail 'loop device unavailable'
  [ -n "$LOOP_NODE" ] || fail 'loop device unavailable'
  losetup "$LOOP_NODE" "$FAT_FILE" || fail 'loop attach failed'
  newfs_msdos -F 16 -c 1 -L CANOEFAT -s "$volume_sectors" "$LOOP_NODE" || fail 'FAT16 format failed'
  fsck_msdos -n "$LOOP_NODE" || fail 'FAT filesystem check failed'
  mount -t vfat -o rw "$LOOP_NODE" "$MOUNT_POINT" || fail 'FAT mount failed'
  MOUNTED=1
  copy_tree
  umount "$MOUNT_POINT" || fail 'FAT unmount failed'
  MOUNTED=0
  losetup -d "$LOOP_NODE" || fail 'loop detach failed'
  LOOP_NODE=
  stamp_and_report || fail 'extent stamp failed'
  REMOVE_ON_FAIL=0
}

case "${1:-}" in
  provision) shift; provision "$@" ;;
  verify) shift; verify_file "${1:-$FAT_SIZE}" ;;
  restamp) [ -f "$FAT_FILE" ] || fail 'file not found'; check_nonsparse || fail 'file allocation is short of its length'; stamp_and_report || fail 'restamp failed' ;;
  *) fail 'usage: provision [size] | verify [size] | restamp' ;;
esac
