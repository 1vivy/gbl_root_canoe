#!/system/bin/sh

set -u

if [ -z "${MODDIR:-}" ]; then
  MODDIR=$(CDPATH= cd -- "$(dirname "$0")/.." 2>/dev/null && pwd)
fi

BY_NAME_DIR=${BY_NAME_DIR:-/dev/block/by-name}
RUNTIME_DIR=${RUNTIME_DIR:-$MODDIR/tmp}
TOKEN=module_blacklist=oplus_secure_guard_new

case "${1:-}" in
  a|_a) slot_suffix=_a ;;
  b|_b) slot_suffix=_b ;;
  *) echo "invalid vendor_boot slot: ${1:-}" >&2; exit 1 ;;
esac

partition="$BY_NAME_DIR/vendor_boot$slot_suffix"
work="$RUNTIME_DIR/vendor_boot_patch.$$"
field="$work/field"
current="$work/current"
new_field="$work/new-field"
verify="$work/verify"

cleanup() {
  rm -rf "$work"
}
trap cleanup EXIT INT TERM HUP

if [ ! -e "$partition" ]; then
  echo "vendor_boot partition not found: $partition" >&2
  exit 1
fi
mkdir -p "$work" || exit 1

if ! blockdev --setrw "$partition" >/dev/null 2>&1; then
  echo "failed to set vendor_boot read-write: $partition" >&2
  exit 1
fi

magic=$(dd if="$partition" bs=1 skip=0 count=8 2>/dev/null) || exit 1
if [ "$magic" != "VNDRBOOT" ]; then
  echo "invalid vendor_boot magic" >&2
  exit 1
fi

if ! dd if="$partition" of="$field" bs=1 skip=28 count=2048 2>/dev/null; then
  echo "failed to read vendor_boot cmdline" >&2
  exit 1
fi
tr -d '\000' < "$field" > "$current" || exit 1
if grep -F -q "$TOKEN" "$current"; then
  echo "already patched"
  exit 0
fi

current_text=$(cat "$current")
new_text="$current_text $TOKEN"
new_bytes=$(printf '%s' "$new_text" | wc -c | tr -d '[:space:]')
case "$new_bytes" in
  ''|*[!0-9]*) echo "failed to measure vendor_boot cmdline" >&2; exit 1 ;;
esac
if [ "$new_bytes" -ge 2048 ]; then
  echo "vendor_boot cmdline is full" >&2
  exit 1
fi

if ! printf '%s' "$new_text" > "$new_field"; then
  echo "failed to compose vendor_boot cmdline" >&2
  exit 1
fi
padding=$((2048 - new_bytes))
if [ "$padding" -gt 0 ] &&
   ! dd if=/dev/zero bs=1 count="$padding" >> "$new_field" 2>/dev/null; then
  echo "failed to pad vendor_boot cmdline" >&2
  exit 1
fi
new_field_bytes=$(wc -c < "$new_field" | tr -d '[:space:]')
[ "$new_field_bytes" = 2048 ] || {
  echo "vendor_boot cmdline field has the wrong size" >&2
  exit 1
}

if ! dd if="$new_field" of="$partition" bs=1 seek=28 conv=notrunc 2>/dev/null; then
  echo "failed to write vendor_boot cmdline" >&2
  exit 1
fi
if ! dd if="$partition" of="$verify" bs=1 skip=28 count=2048 2>/dev/null; then
  echo "failed to reread vendor_boot cmdline" >&2
  exit 1
fi
if ! tr -d '\000' < "$verify" | grep -F -q "$TOKEN"; then
  echo "vendor_boot cmdline verification failed" >&2
  exit 1
fi

echo "patched"
