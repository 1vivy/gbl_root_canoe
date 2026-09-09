#!/bin/sh
# Drive the actual installer presentation with deterministic released choices.
# Native review/apply semantics are qualified in canoe-boot-manager separately.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/module/bin"
export MODPATH="$TMP/module" TMPDIR="$TMP" STAGE_LOG="$TMP/events" STAGE_STATE="$TMP/count"
sed '$d' "$ROOT/targets/magisk_module/module/install-flow.sh" |
  sed "s|/data/adb/modules/fake_bl_efisp/bin/canoe-manager|$TMP/existing-manager|g" > "$TMP/flow.sh"
cat > "$MODPATH/bin/canoe-manager" <<'SH'
#!/bin/sh
set -eu
[ "$1 $2" != 'installer supported' ] || exit 0
confirm=no
for arg in "$@"; do [ "$arg" != --confirm ] || confirm=yes; done
if [ "$confirm" = no ]; then
  count=$(cat "$STAGE_STATE" 2>/dev/null || echo 0)
  count=$((count + 1)); echo "$count" > "$STAGE_STATE"
  if [ "$count" -eq 1 ]; then stage=cleanup; else stage=deploy; fi
  printf 'review:%s\n' "$stage" >> "$STAGE_LOG"
  printf 'Review %s\nSTAGE=%s\nCONFIRM=%064d\n' "$stage" "$stage" "$count"
else
  count=$(cat "$STAGE_STATE")
  if [ "$count" -eq 1 ]; then stage=cleanup; else stage=deploy; fi
  printf 'apply:%s\n' "$stage" >> "$STAGE_LOG"
  [ "${FAIL_CLEANUP:-no}:$stage" != yes:cleanup ] || exit 1
  if [ "$stage" = cleanup ]; then echo 'Old mod files removed. CANOE-BDS has not been installed.'; else echo 'Installation complete.'; fi
fi
SH
chmod +x "$MODPATH/bin/canoe-manager"
cat > "$TMP/run.sh" <<'SH'
#!/bin/sh
set -eu
. "$TMPDIR/flow.sh"
ui_print() { printf '%s\n' "$*"; }
abort() { printf '%s\n' "$*"; exit 9; }
canoe_choose() {
  printf 'prompt:%s\n' "$1" >> "$STAGE_LOG"
  canoe_choice=$(head -n 1 "$TMPDIR/choices")
  tail -n +2 "$TMPDIR/choices" > "$TMPDIR/remaining"
  mv "$TMPDIR/remaining" "$TMPDIR/choices"
}
canoe_install_flow
SH
run_case() {
  name=$1; choices=$2; expected=$3; failure=${4:-no}
  rm -f "$STAGE_STATE"; : > "$STAGE_LOG"
  printf '%s\n' "$choices" > "$TMP/choices"
  result=0
  FAIL_CLEANUP=$failure sh "$TMP/run.sh" > "$TMP/output" 2>&1 || result=$?
  [ "$result" -eq "$expected" ] || { cat "$TMP/output"; echo "FAIL $name: exit $result"; exit 1; }
}
prefix='2
1
1
1'
run_case both "$prefix
2
2" 0
[ "$(sed -n '/^review:\|^apply:/p' "$STAGE_LOG")" = 'review:cleanup
apply:cleanup
review:deploy
apply:deploy' ] || { cat "$STAGE_LOG"; exit 1; }
grep -q 'Installation complete.' "$TMP/output"
run_case cancel_cleanup "$prefix
3" 9
! grep -q '^apply:' "$STAGE_LOG"
run_case timeout_cleanup "$prefix
timeout" 0
! grep -q '^apply:' "$STAGE_LOG"
run_case stop_after_cleanup "$prefix
2
1" 0
grep -q '^apply:cleanup' "$STAGE_LOG"
! grep -q '^apply:deploy' "$STAGE_LOG"
! grep -q 'Installation complete.' "$TMP/output"
run_case cleanup_failure "$prefix
2" 9 yes
! grep -q '^review:deploy' "$STAGE_LOG"
! grep -q 'Installation complete.' "$TMP/output"
cp "$MODPATH/bin/canoe-manager" "$TMP/existing-manager"
run_case module_update '' 0
! grep -q '^review:\|^apply:\|^prompt:' "$STAGE_LOG"
echo 'installer stage fixtures passed: separate cleanup/deployment review, timeout, cancellation, failure and manager update'
