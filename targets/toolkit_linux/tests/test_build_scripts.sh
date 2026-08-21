#!/bin/sh
# Host fixture coverage for the Linux/Android toolkit build scripts.
# Run from the repository root with: sh targets/toolkit_linux/tests/test_build_scripts.sh
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
  flavor=$1
  behavior=$2
  work="$TMP/$flavor-$behavior"
  rm -rf "$work"
  mkdir -p "$work/bin" "$work/efisp" "$work/images"
  cp "$ROOT/targets/toolkit_$flavor/resources/build.sh" "$work/build.sh"
  printf 'synthetic-abl\n' > "$work/images/abl.img"
  printf 'synthetic-vbmeta\n' > "$work/images/vbmeta.img"
  printf 'stale-abl\n' > "$work/efisp/boot.efi"
  printf 'stale-profile\n' > "$work/efisp/boot.efi.gm2p"
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
  flavor=$1
  behavior=$2
  make_fixture "$flavor" "$behavior"
  work="$TMP/$flavor-$behavior"
  runner=bash
  [ "$flavor" = android ] && runner=sh
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
    "$runner" ./build.sh > "$output" 2>&1) || rc=$?
  case "$behavior" in
    ok)
      [ "$rc" -eq 0 ] || fail "$flavor success fixture failed"
      assert_file "$work/efisp/boot.efi"
      assert_file "$work/efisp/boot.efi.gm2p"
      assert_file "$work/efisp/boot.efi.tzmap"
      [ -s "$work/efisp/boot.efi" ] || fail "$flavor produced empty ABL"
      assert_eq "$(wc -c < "$work/efisp/boot.efi.gm2p")" 120 \
        "$flavor profile size"
      assert_eq "$(wc -c < "$work/efisp/boot.efi.tzmap")" 256 \
        "$flavor tzmap size"
      grep -F 'Patched. Outputs:' "$output" >/dev/null || fail "$flavor printed no success"
      assert_order "$work/trace.log" extractfv patch_abl \
        'derive ./images/vbmeta.img' 'validate ./efisp/boot.efi.gm2p' \
        'tzmap-derive ./ABL_original.efi allow=1' \
        'tzmap-validate ./efisp/boot.efi.tzmap'
      pass "$flavor derives matching vbmeta, validates, and emits the triple"
      ;;
    *)
      [ "$rc" -ne 0 ] || fail "$flavor accepted $behavior failure"
      assert_absent "$work/efisp/boot.efi"
      assert_absent "$work/efisp/boot.efi.gm2p"
      assert_absent "$work/efisp/boot.efi.tzmap"
      ! grep -F 'Patched. Outputs:' "$output" >/dev/null || fail "$flavor printed success after $behavior failure"
      pass "$flavor removes the triple after $behavior failure"
      ;;
  esac
}

for flavor in linux android; do
  run_fixture "$flavor" ok
  run_fixture "$flavor" derive
  run_fixture "$flavor" missing-validate
  run_fixture "$flavor" size
  run_fixture "$flavor" wrong-vbmeta
  run_fixture "$flavor" missing-vbmeta
  run_fixture "$flavor" tzmap-derive
  run_fixture "$flavor" tzmap-missing-validate
  run_fixture "$flavor" tzmap-size
done

WINDOWS="$ROOT/targets/toolkit_windows/resources/build.bat"
derive=$(line_of "$WINDOWS" 'mode2_profile.exe derive --vbmeta images\vbmeta.img')
validate=$(line_of "$WINDOWS" 'mode2_profile.exe validate --input efisp\boot.efi.gm2p')
size=$(line_of "$WINDOWS" 'for %%A in (efisp\boot.efi.gm2p) do if not "%%~zA"=="120"')
tz_derive=$(line_of "$WINDOWS" 'abl_tzmap.exe derive ABL_original.efi -o efisp\boot.efi.tzmap')
tz_validate=$(line_of "$WINDOWS" 'abl_tzmap.exe validate efisp\boot.efi.tzmap')
tz_size=$(line_of "$WINDOWS" 'for %%A in (efisp\boot.efi.tzmap) do if not "%%~zA"=="256"')
success=$(line_of "$WINDOWS" 'Patched. Outputs:')
success_exit=$(line_of "$WINDOWS" 'exit /b 0')
[ -n "$derive" ] && [ -n "$validate" ] && [ -n "$size" ] && [ -n "$success" ] &&
  [ -n "$success_exit" ] || fail 'Windows build script lacks profile assertions'
[ -n "$tz_derive" ] && [ -n "$tz_validate" ] && [ -n "$tz_size" ] ||
  fail 'Windows build script lacks tzmap assertions'
[ "$derive" -lt "$validate" ] && [ "$validate" -lt "$size" ] &&
  [ "$size" -lt "$tz_derive" ] && [ "$tz_derive" -lt "$tz_validate" ] &&
  [ "$tz_validate" -lt "$tz_size" ] && [ "$tz_size" -lt "$success" ] &&
  [ "$success" -lt "$success_exit" ] ||
  fail 'Windows derive/validate/size/success checks are out of order'
# The sidecar is derived from the unpatched loader, and installers opt in with
# --allow-incomplete so an un-analysed device still installs.
grep -F 'abl_tzmap.exe derive ABL_original.efi -o efisp\boot.efi.tzmap --allow-incomplete' "$WINDOWS" >/dev/null ||
  fail 'Windows tzmap derive lacks the unpatched loader or --allow-incomplete'
cleanup_count=$(awk 'index($0, "del /q efisp\\boot.efi efisp\\boot.efi.gm2p efisp\\boot.efi.tzmap") { count++ } END { print count + 0 }' "$WINDOWS")
[ "$cleanup_count" -ge 6 ] || fail 'Windows failure paths do not clean all three outputs'
pass 'Windows script statically preserves derive/validate/order/cleanup semantics'

echo 'all toolkit build fixtures passed'
