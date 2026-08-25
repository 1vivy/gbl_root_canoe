#!/system/bin/sh
set -u

MODDIR=${MODDIR:-$(CDPATH= cd -- "$(dirname "$0")/.." 2>/dev/null && pwd)}
PERSIST_MNT=${PERSIST_MNT:-/mnt/vendor/persist}
FAT_FILE=${FAT_FILE:-$PERSIST_MNT/efisp.fat}
PERSIST_DEVICE=${PERSIST_DEVICE:-/dev/block/by-name/persist}
FAT_SIZE=${FAT_SIZE:-16777216}
IMAGE_PATH=${FAT_IMAGE:-}
FIEMAP_HELPER=${FIEMAP_HELPER:-$MODDIR/bin/fiemap}
EXTENT_SOURCE=${EXTENT_SOURCE:-}
RUNTIME_DIR=${RUNTIME_DIR:-$MODDIR/tmp}
RUNS_FILE=$RUNTIME_DIR/fat-runs.$$
RUNLIST_FILE=$RUNTIME_DIR/fat-runlist.$$
TRAILER_FILE=$RUNTIME_DIR/fat-trailer.$$

cleanup() { rm -f "$RUNS_FILE" "$RUNLIST_FILE" "$TRAILER_FILE"; }
trap cleanup EXIT INT TERM HUP
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
free_bytes() {
  if [ -n "${PERSIST_FREE_BYTES:-}" ]; then printf '%s\n' "$PERSIST_FREE_BYTES"; return; fi
  df -Pk "$PERSIST_MNT" 2>/dev/null | awk 'NR == 2 { print $4 * 1024; exit }'
}
check_nonsparse() {
  size=$(file_size "$FAT_FILE") || return 1
  blocks=$(file_blocks "$FAT_FILE") || return 1
  num "$size" && num "$blocks" || return 1
  expected_blocks=$((size / 512))
  [ "$blocks" -ge "$expected_blocks" ]
}

parse_extents() {
  awk '
    function clean(value) { gsub(/[ \t]/, "", value); return value }
    /^[ \t]*[0-9]+:[0-9]+[ \t]*$/ { print $0; next }
    /^[ \t]*[0-9]+:/ {
      count = split($0, fields, ":"); if (count < 4) next
      physical = clean(fields[3]); if (physical !~ /^[0-9]+\.\.[0-9]+$/) next
      first = physical; sub(/\.\..*$/, "", first)
      last = physical; sub(/^.*\.\./, "", last)
      if (first ~ /^[0-9]+$/ && last ~ /^[0-9]+$/ && last >= first)
        print first ":" (last - first + 1)
    }
  '
}
resolve_runs() {
  source=${EXTENT_SOURCE:-}; raw=$RUNTIME_DIR/fat-extents.$$
  mkdir -p "$RUNTIME_DIR" || return 1; : > "$raw"
  if [ -z "$source" ] && command -v filefrag >/dev/null 2>&1; then source=filefrag; fi
  if [ -z "$source" ] && command -v debugfs >/dev/null 2>&1; then source=debugfs; fi
  if [ -z "$source" ] && [ -x "$FIEMAP_HELPER" ]; then source=fiemap; fi
  case "$source" in
    filefrag) filefrag -v "$FAT_FILE" > "$raw" 2>&1 || return 1 ;;
    debugfs) debugfs -R "filefrag -v $FAT_FILE" "$PERSIST_DEVICE" > "$raw" 2>&1 || return 1 ;;
    fiemap) "$FIEMAP_HELPER" "$FAT_FILE" > "$raw" 2>&1 || return 1 ;;
    *) return 1 ;;
  esac
  lower=$(tr '[:upper:]' '[:lower:]' < "$raw")
  case "$lower" in *unwritten*|*delalloc*|*inline*|*encrypted*|*compressed*) return 1 ;; esac
  parse_extents < "$raw" > "$RUNS_FILE"; rm -f "$raw"
  actual_bytes=$(file_size "$FAT_FILE") || return 1
  needed=$(( (actual_bytes + 4095) / 4096 ))
  covered=$(awk -F: '{ total += $2 } END { print total + 0 }' "$RUNS_FILE")
  [ "$covered" -ge "$needed" ] || return 1
  RUN_SOURCE=$source; RUN_COUNT=$(wc -l < "$RUNS_FILE" | awk '{print $1}'); RUNS=$(tr '\n' ',' < "$RUNS_FILE" | sed 's/,$//')
}
make_runlist() {
  mkdir -p "$RUNTIME_DIR" || return 1
  : > "$RUNLIST_FILE" || return 1
  while IFS=: read -r physical count; do
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
  : > "$TRAILER_FILE" || return 1; dd if=/dev/zero of="$TRAILER_FILE" bs=4096 count=1 2>/dev/null || return 1
  printf 'CANOEFT1' | dd of="$TRAILER_FILE" bs=1 seek=0 conv=notrunc 2>/dev/null || return 1
  write_le "$size" 8 | dd of="$TRAILER_FILE" bs=1 seek=8 conv=notrunc 2>/dev/null || return 1
  write_le "$volume" 8 | dd of="$TRAILER_FILE" bs=1 seek=16 conv=notrunc 2>/dev/null || return 1
  write_le "$RUN_COUNT" 4 | dd of="$TRAILER_FILE" bs=1 seek=24 conv=notrunc 2>/dev/null || return 1
  hex_bytes "$hash" | dd of="$TRAILER_FILE" bs=1 seek=32 conv=notrunc 2>/dev/null || return 1
  if [ $((volume % 4096)) -eq 0 ]; then dd if="$TRAILER_FILE" of="$FAT_FILE" bs=4096 seek=$((volume / 4096)) conv=notrunc 2>/dev/null; else dd if="$TRAILER_FILE" of="$FAT_FILE" bs=1 seek="$volume" conv=notrunc 2>/dev/null; fi
}
stamp_and_report() {
  size=$(file_size "$FAT_FILE") || return 1
  resolve_runs || return 1
  make_runlist || return 1
  write_trailer "$size" || return 1
  verify_file "$size"
}
verify_file() {
  expected=$(size_bytes "${1:-$FAT_SIZE}") || { say "ERROR=invalid expected size" >&2; return 1; }; exists=0; nonsparse=0; size_ok=0; stamp=0; source=none; RUN_COUNT=0; RUNS=
  [ -f "$FAT_FILE" ] && exists=1
  if [ "$exists" = 1 ]; then
    check_nonsparse && nonsparse=1 || :; actual_size=$(file_size "$FAT_FILE") || actual_size=0
    [ "$actual_size" = "$expected" ] && size_ok=1 || :
    if [ "$nonsparse" = 1 ] && resolve_runs; then
      source=$RUN_SOURCE; make_runlist || :; hash=$(sha256_file "$RUNLIST_FILE" 2>/dev/null || printf '')
      base=$((actual_size - 4096)); magic=$(read_hex "$base" 8); stored_size=$(read_hex $((base + 8)) 8); stored_volume=$(read_hex $((base + 16)) 8)
      stored_count=$(read_hex $((base + 24)) 4); stored_hash=$(read_hex $((base + 32)) 32)
      [ "$magic" = 43414e4f45465431 ] && [ "$stored_size" = "$(le_hex "$actual_size" 8)" ] && [ "$stored_volume" = "$(le_hex "$base" 8)" ] && [ "$stored_count" = "$(le_hex "$RUN_COUNT" 4)" ] && [ "$stored_hash" = "$hash" ] && stamp=1 || :
    fi
  fi
  say "EXISTS=$exists|NON_SPARSE=$nonsparse|SIZE_OK=$size_ok|RUN_COUNT=$RUN_COUNT|RUNS=${RUNS:-}|EXTENT_SOURCE=$source|STAMP_VALID=$stamp|STAMP_MATCH=$stamp"
  [ "$exists" = 1 ] && [ "$nonsparse" = 1 ] && [ "$size_ok" = 1 ] && [ "$stamp" = 1 ]
}
provision() {
  raw_size=${1:-$FAT_SIZE}; image=${2:-$IMAGE_PATH}
  if [ "$#" -eq 1 ] && [ -f "$raw_size" ]; then image=$raw_size; raw_size=$FAT_SIZE; fi
  requested=$(size_bytes "$raw_size") || fail "invalid size"
  [ "$requested" -ge 8192 ] || fail "size too small"; [ $((requested % 4096)) -eq 0 ] || fail "size must be a 4096-byte multiple"
  [ ! -e "$FAT_FILE" ] || fail "file already exists: $FAT_FILE"; free=$(free_bytes); num "$free" || fail "persist free space unavailable"; [ "$free" -ge "$requested" ] || fail "insufficient persist free space"
  mkdir -p "$PERSIST_MNT" || fail "persist mount unavailable"
  if [ $((requested % 1048576)) -eq 0 ]; then
    dd if=/dev/zero of="$FAT_FILE" bs=1M count=$((requested / 1048576)) conv=fsync 2>/dev/null || { rm -f "$FAT_FILE"; fail "zero fill failed"; }
  else
    dd if=/dev/zero of="$FAT_FILE" bs=4096 count=$((requested / 4096)) conv=fsync 2>/dev/null || { rm -f "$FAT_FILE"; fail "zero fill failed"; }
  fi
  if [ -n "$image" ]; then
    [ -f "$image" ] || { rm -f "$FAT_FILE"; fail "image not found"; }; image_size=$(file_size "$image") || { rm -f "$FAT_FILE"; fail "image size unavailable"; }
    [ "$image_size" = "$requested" ] || { rm -f "$FAT_FILE"; fail "image size mismatch"; }
    dd if="$image" of="$FAT_FILE" bs=4194304 conv=notrunc 2>/dev/null || { rm -f "$FAT_FILE"; fail "image copy failed"; }
  fi
  check_nonsparse || { rm -f "$FAT_FILE"; fail "file is sparse"; }; stamp_and_report || { rm -f "$FAT_FILE"; fail "extent stamp failed"; }
}
case "${1:-}" in
  provision) shift; provision "$@" ;;
  verify) shift; verify_file "${1:-$FAT_SIZE}" ;;
  restamp) [ -f "$FAT_FILE" ] || fail "file not found"; check_nonsparse || fail "file is sparse"; stamp_and_report || fail "restamp failed" ;;
  *) fail "usage: $0 provision [size] [image] | verify [size] | restamp" ;;
esac
