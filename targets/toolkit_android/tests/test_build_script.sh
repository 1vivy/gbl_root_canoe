#!/bin/sh
# Run from the repository root with: sh targets/toolkit_android/tests/test_build_script.sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/canoe-toolkit-build.XXXXXX")
trap 'rm -rf "$TMP"' EXIT INT TERM HUP

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "ok - $*"; }

make_fixture() {
  name=$1
  work="$TMP/$name"
  rm -rf "$work"
  mkdir -p "$work/bin" "$work/images" "$work/bootroot" "$work/dev/by-name"
  cp "$ROOT/targets/toolkit_android/resources/build.sh" "$work/build.sh"
  printf 'partition-abl-a\n' > "$work/dev/by-name/abl_a"
  printf 'partition-vbmeta-a\n' > "$work/dev/by-name/vbmeta_a"
  printf 'partition-abl-b\n' > "$work/dev/by-name/abl_b"
  printf 'partition-vbmeta-b\n' > "$work/dev/by-name/vbmeta_b"
  printf 'supplied-abl\n' > "$work/supplied-abl.img"
  printf 'supplied-vbmeta\n' > "$work/supplied-vbmeta.img"
  : > "$work/trace.log"

  cat > "$work/bin/id" <<'EOF'
#!/bin/sh
[ "${TEST_ROOT:-root}" = nonroot ] && { echo 1000; exit 0; }
[ "${1-}" = -u ] && { echo 0; exit 0; }
exit 1
EOF
  cat > "$work/bin/getprop" <<'EOF'
#!/bin/sh
[ "${1-}" = ro.boot.slot_suffix ] && { echo "${TEST_SLOT:-_a}"; exit 0; }
exit 1
EOF
  cat > "$work/bin/extractfv" <<'EOF'
#!/bin/sh
set -eu
out= input=
while [ "$#" -gt 0 ]; do
  case "$1" in
    -o) out=$2; shift 2 ;;
    *) input=$1; shift ;;
  esac
done
printf 'extractfv %s\n' "$input" >> "$TRACE"
case "$(cat "$input")" in
  partition-*|supplied-abl*) ;;
  *) exit 41 ;;
esac
printf 'extracted-linux-loader\n' > "$out/LinuxLoader.efi"
EOF
  cat > "$work/bin/patch_abl" <<'EOF'
#!/bin/sh
set -eu
[ "$(cat "$1")" = extracted-linux-loader ] || exit 42
printf 'patch_abl %s\n' "$1" >> "$TRACE"
printf 'patched-abl-output\n' > "$2"
EOF
  cat > "$work/bin/mode2_profile" <<'EOF'
#!/bin/sh
set -eu
command=${1-}
shift || true
case "$command" in
  derive)
    vbmeta= out=
    while [ "$#" -gt 0 ]; do
      case "$1" in
        --vbmeta) vbmeta=$2; shift 2 ;;
        --out) out=$2; shift 2 ;;
        *) exit 43 ;;
      esac
    done
    printf 'profile-derive %s\n' "$vbmeta" >> "$TRACE"
    [ "${PROFILE_BEHAVIOR:-ok}" != derive ] || exit 44
    dd if=/dev/zero of="$out" bs=120 count=1 2>/dev/null
    ;;
  validate)
    [ "${1-}" = --input ] || exit 47
    input=${2-}
    printf 'profile-validate %s\n' "$input" >> "$TRACE"
    [ "${PROFILE_BEHAVIOR:-ok}" != validate ] || exit 45
    [ "$(wc -c < "$input")" -eq 120 ] || exit 46
    ;;
  *) exit 49 ;;
esac
EOF
  cat > "$work/bin/abl_tzmap" <<'EOF'
#!/bin/sh
set -eu
command=${1-}
shift || true
case "$command" in
  derive)
    abl= out=
    while [ "$#" -gt 0 ]; do
      case "$1" in
        -o) out=$2; shift 2 ;;
        --allow-incomplete) shift ;;
        *) abl=$1; shift ;;
      esac
    done
    printf 'tzmap-derive %s\n' "$abl" >> "$TRACE"
    [ "${TZMAP_BEHAVIOR:-ok}" != derive ] || exit 51
    [ "$(cat "$abl")" = extracted-linux-loader ] || exit 52
    dd if=/dev/zero of="$out" bs=256 count=1 2>/dev/null
    ;;
  validate)
    printf 'tzmap-validate %s\n' "${1-}" >> "$TRACE"
    [ "${TZMAP_BEHAVIOR:-ok}" != validate ] || exit 53
    [ "$(wc -c < "$1")" -eq 256 ] || exit 54
    ;;
  *) exit 55 ;;
esac
EOF
  chmod +x "$work/bin/id" "$work/bin/getprop" "$work/bin/extractfv" \
    "$work/bin/patch_abl" "$work/bin/mode2_profile" "$work/bin/abl_tzmap"

  # This fixture stands in for the shared transaction and records every
  # positional argument.  Exactly two arguments proves no efisp block device
  # or backup path can be written by the temporary-root package.
  cat > "$work/canoe_device_install.sh" <<'EOF'
#!/bin/sh
printf 'install argc=%s mode=%s slot=%s allow=%s\n' "$#" "${CANOE_MODE-}" \
  "${CANOE_ACTIVE_SLOT-}" "${CANOE_ALLOW_NEW_SIGNER-}" >> "$TRACE"
printf 'install arg1=%s\ninstall arg2=%s\n' "${1-}" "${2-}" >> "$TRACE"
[ "$#" -eq 2 ] || exit 61
[ -d "$1" ] || exit 62
[ "$2" = "$CANOE_BOOT_ROOT" ] || exit 63
EOF
  : > "$work/canoe_boot_entry.sh"
}

run_build() {
  work=$1
  shift
  (cd "$work" && PATH="$work/bin:$PATH" TRACE="$work/trace.log" \
    CANOE_BOOT_ROOT="$work/bootroot" CANOE_BY_NAME_DIR="$work/dev/by-name" \
    TEST_ROOT="${TEST_ROOT:-root}" TEST_SLOT="${TEST_SLOT:-_a}" \
    PROFILE_BEHAVIOR="${PROFILE_BEHAVIOR:-ok}" \
    TZMAP_BEHAVIOR="${TZMAP_BEHAVIOR:-ok}" sh ./build.sh "$@")
}
make_fixture nonroot
work="$TMP/nonroot"
if TEST_ROOT=nonroot run_build "$work" >"$work/output.log" 2>&1; then
  fail 'non-root invocation was accepted'
fi
grep -F 'root is required' "$work/output.log" >/dev/null || fail 'root refusal missing'
[ ! -e "$work/bootroot/canoe-stage" ] || fail 'non-root invocation wrote a staging directory'
pass 'non-root invocation is refused before writing'

make_fixture bad-slot
work="$TMP/bad-slot"
if TEST_SLOT=_c run_build "$work" >"$work/output.log" 2>&1; then
  fail 'invalid active slot was accepted'
fi
grep -F 'must be _a or _b' "$work/output.log" >/dev/null || fail 'slot refusal missing'
[ ! -e "$work/bootroot/canoe-stage" ] || fail 'invalid slot wrote a staging directory'
pass 'invalid active slot is refused before writing'


make_fixture defaults
work="$TMP/defaults"
TEST_SLOT=_b run_build "$work"
grep -F "extractfv $work/dev/by-name/abl_b" "$work/trace.log" >/dev/null || \
  fail 'default ABL did not use the active slot partition'
grep -F "profile-derive $work/dev/by-name/vbmeta_b" "$work/trace.log" >/dev/null || \
  fail 'default vbmeta did not use the active slot partition'
grep -F 'install argc=2 mode=1 slot=_b allow=' "$work/trace.log" >/dev/null || \
  fail 'default transaction was not mode 1 and tree-only'
pass 'defaults derive from active-slot partitions and invoke the transaction tree-only'

make_fixture supplied
work="$TMP/supplied"
TEST_SLOT=_a run_build "$work" --mode 0 --abl "$work/supplied-abl.img" \
  --vbmeta "$work/supplied-vbmeta.img"
grep -F "extractfv $work/supplied-abl.img" "$work/trace.log" >/dev/null || \
  fail 'supplied ABL was not fed to extractfv'
grep -F "profile-derive $work/supplied-vbmeta.img" "$work/trace.log" >/dev/null || \
  fail 'supplied vbmeta was not fed to mode2_profile'
grep -F 'install argc=2 mode=0 slot=_a allow=1' "$work/trace.log" >/dev/null || \
  fail 'supplied vbmeta did not allow the signer gate transaction'
pass 'supplied ABL and vbmeta feed derivation and enable only the signer allowance'

make_fixture mode2
work="$TMP/mode2"
if TEST_SLOT=_a run_build "$work" --mode 2 >"$work/output.log" 2>&1; then
  fail 'mode 2 was accepted'
fi
grep -F 'Mode 2 belongs to the module/WebUI path' "$work/output.log" >/dev/null || \
  fail 'mode 2 refusal did not state its owner path'
[ ! -e "$work/bootroot/canoe-stage" ] || fail 'mode 2 created a staging directory'
pass 'mode 2 is refused for the temporary-root package'

make_fixture empty-supplied
work="$TMP/empty-supplied"
: > "$work/empty.img"
if TEST_SLOT=_a run_build "$work" --abl "$work/empty.img" >"$work/output.log" 2>&1; then
  fail 'empty supplied ABL was accepted'
fi
grep -F 'ABL input is missing or empty' "$work/output.log" >/dev/null || \
  fail 'empty supplied ABL refusal was not reported'
[ ! -e "$work/bootroot/canoe-stage" ] || fail 'empty supplied image wrote a staging directory'
[ ! -s "$work/trace.log" ] || fail 'empty supplied image ran a derivation command'
pass 'empty supplied images are refused before any write'

make_fixture derive-failure
work="$TMP/derive-failure"
if PROFILE_BEHAVIOR=derive TEST_SLOT=_a run_build "$work" >"$work/output.log" 2>&1; then
  fail 'derivation failure was accepted'
fi
[ ! -e "$work/bootroot/canoe-stage/boot.efi" ] || fail 'failed derive left boot.efi'
[ ! -e "$work/bootroot/canoe-stage/boot.efi.gm2p" ] || fail 'failed derive left gm2p'
[ ! -e "$work/bootroot/canoe-stage/boot.efi.tzmap" ] || fail 'failed derive left tzmap'
pass 'derivation failures remove the whole staged triple'

echo 'all Android toolkit build fixtures passed'
