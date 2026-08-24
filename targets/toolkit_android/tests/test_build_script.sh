#!/bin/sh
# Host fixture coverage for the Android toolkit build script.
# Run from the repository root with: sh targets/toolkit_android/tests/test_build_script.sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/canoe-toolkit-build.XXXXXX")
trap 'rm -rf "$TMP"' EXIT INT TERM HUP

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "ok - $*"; }
assert_file() { [ -f "$1" ] || fail "missing file: $1"; }
assert_absent() { [ ! -e "$1" ] || fail "stale output: $1"; }
assert_eq() { [ "$1" = "$2" ] || fail "$3 (got '$1', want '$2')"; }
line_of() { grep -nF -m 1 -- "$2" "$1" | cut -d: -f1; }
assert_order() {
  trace=$1
  previous=0
  shift
  for marker in "$@"; do
    current=$(line_of "$trace" "$marker")
    [ -n "$current" ] || fail "missing trace marker '$marker'"
    [ "$current" -gt "$previous" ] || fail "trace order broke at '$marker'"
    previous=$current
  done
}

make_fixture() {
  behavior=$1
  work="$TMP/$behavior"
  rm -rf "$work"
  mkdir -p "$work/bin" "$work/efisp" "$work/images"
  cp "$ROOT/targets/toolkit_android/resources/build.sh" "$work/build.sh"
  printf 'synthetic-abl\n' > "$work/images/abl.img"
  printf 'synthetic-vbmeta\n' > "$work/images/vbmeta.img"
  # Seed every output: each failure path must remove all three, not just its own.
  printf 'stale-abl\n' > "$work/efisp/boot.efi"
  printf 'stale-profile\n' > "$work/efisp/boot.efi.gm2p"
  printf 'stale-tzmap\n' > "$work/efisp/boot.efi.tzmap"
  printf '%s\n' "$work" "$behavior" > "$work/fixture.env"
  TRACE="$work/trace.log" EXPECTED_ABL=synthetic-abl EXPECTED_VBMETA=synthetic-vbmeta
  export TRACE EXPECTED_ABL EXPECTED_VBMETA

  cat > "$work/bin/extractfv" <<'EOF'
#!/bin/sh
set -eu
input= out=.
while [ "$#" -gt 0 ]; do
  case "$1" in
    -o) out=$2; shift 2 ;;
    *) [ -n "$input" ] || input=$1; shift ;;
  esac
done
[ "$(cat "$input")" = "$EXPECTED_ABL" ] || exit 41
printf 'extractfv\n' >> "$TRACE"
printf 'extracted-linux-loader\n' > "$out/LinuxLoader.efi"
EOF
  cat > "$work/bin/patch_abl" <<'EOF'
#!/bin/sh
set -eu
[ "$(cat "$1")" = "extracted-linux-loader" ] || exit 42
printf 'patch_abl\n' >> "$TRACE"
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
    printf 'derive %s\n' "$vbmeta" >> "$TRACE"
    [ "$vbmeta" = "./images/vbmeta.img" ] || exit 44
    [ "$(cat "$vbmeta")" = "$EXPECTED_VBMETA" ] || exit 45
    [ "${PROFILE_BEHAVIOR:-ok}" = derive ] && exit 46
    size=120
    [ "${PROFILE_BEHAVIOR:-ok}" = size ] && size=119
    dd if=/dev/zero of="$out" bs="$size" count=1 2>/dev/null
    ;;
  validate)
    input=
    while [ "$#" -gt 0 ]; do
      case "$1" in
        --input) input=$2; shift 2 ;;
        *) exit 47 ;;
      esac
    done
    printf 'validate %s\n' "$input" >> "$TRACE"
    [ "${PROFILE_BEHAVIOR:-ok}" = missing-validate ] && exit 127
    if [ "${PROFILE_BEHAVIOR:-ok}" != size ]; then
      [ "$(wc -c < "$input")" -eq 120 ] || exit 48
    fi
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
    abl= out= allow=0
    while [ "$#" -gt 0 ]; do
      case "$1" in
        -o) out=$2; shift 2 ;;
        --allow-incomplete) allow=1; shift ;;
        *) [ -n "$abl" ] || abl=$1; shift ;;
      esac
    done
    printf 'tzmap-derive %s allow=%s\n' "$abl" "$allow" >> "$TRACE"
    # Installers must opt in, or an un-analysed device would fail to install.
    [ "$allow" = 1 ] || exit 50
    # The sidecar is derived from the unpatched loader, never the patched ABL.
    [ "$(cat "$abl")" = "extracted-linux-loader" ] || exit 51
    [ "${TZMAP_BEHAVIOR:-ok}" = derive ] && exit 52
    size=256
    [ "${TZMAP_BEHAVIOR:-ok}" = size ] && size=255
    dd if=/dev/zero of="$out" bs="$size" count=1 2>/dev/null
    ;;
  validate)
    input=${1-}
    printf 'tzmap-validate %s\n' "$input" >> "$TRACE"
    [ "${TZMAP_BEHAVIOR:-ok}" = missing-validate ] && exit 127
    if [ "${TZMAP_BEHAVIOR:-ok}" != size ]; then
      [ "$(wc -c < "$input")" -eq 256 ] || exit 53
    fi
    ;;
  *) exit 54 ;;
esac
EOF
  chmod +x "$work/bin/extractfv" "$work/bin/patch_abl" "$work/bin/mode2_profile" \
    "$work/bin/abl_tzmap"
  if [ "$behavior" = wrong-vbmeta ]; then
    printf 'wrong-vbmeta\n' > "$work/images/vbmeta.img"
  elif [ "$behavior" = missing-vbmeta ]; then
    rm -f "$work/images/vbmeta.img"
  fi
}

run_fixture() {
  behavior=$1
  make_fixture "$behavior"
  work="$TMP/$behavior"
  output="$work/output.log"
  rc=0
  profile_behavior="$behavior"
  tzmap_behavior=ok
  case "$behavior" in
    tzmap-*) profile_behavior=ok; tzmap_behavior=${behavior#tzmap-} ;;
  esac
  (cd "$work" && PROFILE_BEHAVIOR="$profile_behavior" \
    TZMAP_BEHAVIOR="$tzmap_behavior" TRACE="$work/trace.log" \
    EXPECTED_ABL=synthetic-abl EXPECTED_VBMETA=synthetic-vbmeta \
    sh ./build.sh > "$output" 2>&1) || rc=$?
  case "$behavior" in
    ok)
      [ "$rc" -eq 0 ] || fail "Android success fixture failed"
      assert_file "$work/efisp/boot.efi"
      assert_file "$work/efisp/boot.efi.gm2p"
      assert_file "$work/efisp/boot.efi.tzmap"
      [ -s "$work/efisp/boot.efi" ] || fail "Android produced empty ABL"
      assert_eq "$(wc -c < "$work/efisp/boot.efi.gm2p")" 120 \
        "Android profile size"
      assert_eq "$(wc -c < "$work/efisp/boot.efi.tzmap")" 256 \
        "Android tzmap size"
      grep -F 'Patched. Outputs:' "$output" >/dev/null || fail "Android printed no success"
      assert_order "$work/trace.log" extractfv patch_abl \
        'derive ./images/vbmeta.img' 'validate ./efisp/boot.efi.gm2p' \
        'tzmap-derive ./ABL_original.efi allow=1' \
        'tzmap-validate ./efisp/boot.efi.tzmap'
      pass 'Android derives matching vbmeta, validates, and emits the triple'
      ;;
    *)
      [ "$rc" -ne 0 ] || fail "Android accepted $behavior failure"
      assert_absent "$work/efisp/boot.efi"
      assert_absent "$work/efisp/boot.efi.gm2p"
      assert_absent "$work/efisp/boot.efi.tzmap"
      ! grep -F 'Patched. Outputs:' "$output" >/dev/null || fail "Android printed success after $behavior failure"
      pass "Android removes the triple after $behavior failure"
      ;;
  esac
}

run_fixture ok
run_fixture derive
run_fixture missing-validate
run_fixture size
run_fixture wrong-vbmeta
run_fixture missing-vbmeta
run_fixture tzmap-derive
run_fixture tzmap-missing-validate
run_fixture tzmap-size

echo 'all Android toolkit build fixtures passed'
