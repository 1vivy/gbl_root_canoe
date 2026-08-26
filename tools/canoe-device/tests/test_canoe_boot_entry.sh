#!/bin/sh
# Fixtures for canoe_boot_entry.sh, the single writer of canoe.cfg.
#
# The writer is the one place in the tree that turns install policy into the
# BDS's config grammar, so these cases pin the properties every caller - the
# host toolkit and the KernelSU module - depends on:
#
#   1  a fresh set produces a grammar-valid document at generation 1
#   2  an upsert preserves every other entry, including a hand-added one
#   3  an omitted mode keeps the entry's own mode; a new entry inherits global
#   4  the global keys are settable and otherwise preserved
#   5  remove drops the entry and re-points a default that named it
#   6  refusals: id, role, title, image, mode, missing entry, 25th entry
#   7  a lenient read drops what the BDS would reject, and still writes
#   8  show writes nothing and does not bump the generation
#   9  mode re-modes an existing entry only, and refuses anything else
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
SCRIPT="$ROOT/tools/canoe-device/canoe_boot_entry.sh"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/canoe-entry.XXXXXX")
trap 'rm -rf "$TMP"' EXIT INT TERM HUP

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "ok - $*"; }
temps() { find "$1" -maxdepth 1 -name '.canoe.cfg.*' -print 2>/dev/null | wc -l | tr -d ' '; }
value() { awk -v key="$2" '$1 == key { print $2; exit }' "$1"; }
entry_field() {
  awk -v wanted="$2" -v key="$3" '
    $1 == "entry" { here = ($2 == wanted); next }
    here && $1 == key { print $2; exit }
  ' "$1"
}

[ -f "$SCRIPT" ] || fail "missing $SCRIPT"

# root <case> -- a fresh empty boot root
root() {
  D="$TMP/$1"
  rm -rf "$D"
  mkdir -p "$D"
  CFG="$D/canoe.cfg"
}

entry() {
  OUT="$TMP/out"; ERR="$TMP/err"
  rc=0
  sh "$SCRIPT" "$@" >"$OUT" 2>"$ERR" || rc=$?
  return "$rc"
}

# ------------------------------------------------------------------ case 1 --
root c1
entry set "$D" --id android-a --title 'Android (slot A)' --image boot.efi \
  --role active --mode 0 --global-mode 0 --default || fail "1: refused: $(cat "$ERR")"
[ "$(value "$CFG" version)" = 1 ] || fail '1: version is not 1'
[ "$(value "$CFG" generation)" = 1 ] || fail '1: generation did not start at 1'
[ "$(value "$CFG" timeout)" = 5 ] || fail '1: timeout default lost'
[ "$(value "$CFG" default)" = android-a ] || fail '1: default not set'
[ "$(value "$CFG" mode)" = 0 ] || fail '1: global mode not applied'
[ "$(value "$CFG" devinfo-repair)" = asneeded ] || fail '1: devinfo-repair default lost'
[ "$(entry_field "$CFG" android-a image)" = boot.efi ] || fail '1: image wrong'
[ "$(entry_field "$CFG" android-a mode)" = 0 ] || fail '1: entry mode wrong'
[ "$(entry_field "$CFG" android-a role)" = active ] || fail '1: role wrong'
grep -q 'CANOE-MARK: entry-set id=android-a role=active mode=0 generation=1' "$OUT" ||
  fail '1: no receipt mark'
[ "$(temps "$D")" = 0 ] || fail '1: temp files left behind'
pass 'a fresh set produces a grammar-valid document at generation 1'

# ------------------------------------------------------------------ case 2 --
# The property the whole design rests on: installing must not erase an entry
# somebody else put there.
root c2
entry set "$D" --id android-a --title 'Android (slot A)' --image boot.efi --role active --default ||
  fail "2: first set refused: $(cat "$ERR")"
printf 'entry lineage\n  title LineageOS\n  image roms/lineage.efi\n  mode 2\n  role other\n' >> "$CFG"
entry set "$D" --id android-b --title 'Android (slot B)' --image boot_b.efi --role inactive ||
  fail "2: second set refused: $(cat "$ERR")"
entry set "$D" --id android-a --title 'Android (slot A)' --image boot.efi --role active --default ||
  fail "2: re-set refused: $(cat "$ERR")"
[ "$(entry_field "$CFG" lineage image)" = roms/lineage.efi ] || fail '2: custom entry lost'
[ "$(entry_field "$CFG" lineage mode)" = 2 ] || fail '2: custom entry mode rewritten'
[ "$(entry_field "$CFG" lineage role)" = other ] || fail '2: custom entry role rewritten'
[ "$(entry_field "$CFG" android-b role)" = inactive ] || fail '2: inactive entry lost'
[ "$(value "$CFG" generation)" = 3 ] || fail '2: generation did not bump once per write'
[ "$(grep -c '^entry ' "$CFG")" = 3 ] || fail '2: entry count wrong'
pass 'an upsert preserves every other entry, including a hand-added one'

# ------------------------------------------------------------------ case 3 --
root c3
entry set "$D" --id android-a --title 'A' --image boot.efi --role active --mode 0 --default ||
  fail "3: refused: $(cat "$ERR")"
entry set "$D" --id android-a --title 'A renamed' --image boot.efi --role active ||
  fail "3: re-set refused: $(cat "$ERR")"
[ "$(entry_field "$CFG" android-a mode)" = 0 ] || fail '3: omitted mode did not keep the entry mode'
[ "$(entry_field "$CFG" android-a title)" = 'A' ] || fail '3: title did not change'
entry set "$D" --id android-b --title 'B' --image boot_b.efi --role inactive ||
  fail "3: new entry refused: $(cat "$ERR")"
[ "$(entry_field "$CFG" android-b mode)" = "$(value "$CFG" mode)" ] ||
  fail '3: new entry did not inherit the global mode'
pass 'an omitted mode keeps the entry mode; a new entry inherits the global'

# ------------------------------------------------------------------ case 4 --
root c4
entry set "$D" --id android-a --title 'A' --image boot.efi --role active --default ||
  fail "4: refused: $(cat "$ERR")"
entry set "$D" --id android-a --title 'A' --image boot.efi --role active \
  --global-mode 2 --timeout 30 --devinfo-repair never || fail "4: globals refused: $(cat "$ERR")"
[ "$(value "$CFG" mode)" = 2 ] || fail '4: global mode not applied'
[ "$(value "$CFG" timeout)" = 30 ] || fail '4: timeout not applied'
[ "$(value "$CFG" devinfo-repair)" = never ] || fail '4: devinfo-repair not applied'
entry set "$D" --id android-b --title 'B' --image boot_b.efi --role inactive ||
  fail "4: later set refused: $(cat "$ERR")"
[ "$(value "$CFG" timeout)" = 30 ] || fail '4: timeout not preserved'
[ "$(value "$CFG" devinfo-repair)" = never ] || fail '4: devinfo-repair not preserved'
pass 'the global keys are settable and otherwise preserved'

# ------------------------------------------------------------------ case 5 --
root c5
entry set "$D" --id android-a --title 'A' --image boot.efi --role active ||
  fail "5: refused: $(cat "$ERR")"
entry set "$D" --id android-backup --title 'Previous' --image boot_backup.efi \
  --role backup --default || fail "5: backup refused: $(cat "$ERR")"
[ "$(value "$CFG" default)" = android-backup ] || fail '5: default not moved'
entry remove "$D" --id android-backup || fail "5: remove refused: $(cat "$ERR")"
grep -q '^entry android-backup$' "$CFG" && fail '5: entry survived removal'
[ "$(value "$CFG" default)" = android-a ] ||
  fail '5: default was not re-pointed at the active entry'
grep -q 'CANOE-MARK: entry-removed id=android-backup' "$OUT" || fail '5: no removal mark'
pass 'remove drops the entry and re-points a default that named it'

# ------------------------------------------------------------------ case 6 --
root c6
entry set "$D" --id android-a --title 'A' --image boot.efi --role active --default ||
  fail "6: refused: $(cat "$ERR")"
before=$(cat "$CFG")
refuse() {
  description=$1
  shift
  if entry "$@"; then fail "6: accepted $description"; fi
  grep -q '^canoe-entry: error: ' "$ERR" || fail "6: $description gave no diagnosis"
  [ "$(cat "$CFG")" = "$before" ] || fail "6: $description changed canoe.cfg"
  [ "$(temps "$D")" = 0 ] || fail "6: $description left temp files"
}
refuse 'an invalid id'    set "$D" --id 'bad id' --title 'X' --image b.efi --role active
refuse 'a long id'        set "$D" --id 0123456789012345678901234567890123 --title 'X' --image b.efi --role active
refuse 'an invalid role'  set "$D" --id ok --title 'X' --image b.efi --role banana
refuse 'an empty title'   set "$D" --id ok --title '' --image b.efi --role active
refuse 'an unprintable title' set "$D" --id ok --title "$(printf 'a\tb')" --image b.efi --role active
refuse 'a long title'     set "$D" --id ok --title 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' --image b.efi --role active
refuse 'a parent path'    set "$D" --id ok --title 'X' --image ../escape.efi --role active
refuse 'a dot path'       set "$D" --id ok --title 'X' --image ./b.efi --role active
refuse 'a doubled slash'  set "$D" --id ok --title 'X' --image 'dir//b.efi' --role active
refuse 'a trailing slash' set "$D" --id ok --title 'X' --image 'dir/' --role active
refuse 'an empty image'   set "$D" --id ok --title 'X' --image '' --role active
refuse 'a mode of 9'      set "$D" --id ok --title 'X' --image b.efi --role active --mode 9
refuse 'a timeout of 61'  set "$D" --id ok --title 'X' --image b.efi --role active --timeout 61
refuse 'a bad repair'     set "$D" --id ok --title 'X' --image b.efi --role active --devinfo-repair maybe
refuse 'a missing entry'  remove "$D" --id nope
refuse 'an unknown option' set "$D" --id ok --title 'X' --image b.efi --role active --frobnicate
pass 'refusals name the reason and leave canoe.cfg untouched'

# ------------------------------------------------------------------ case 6b --
# 24 entries is the BDS ceiling; the 25th must be refused, not silently dropped.
root c6b
index=1
while [ "$index" -le 24 ]; do
  entry set "$D" --id "e$index" --title "Entry $index" --image "boot$index.efi" --role other ||
    fail "6b: entry $index refused: $(cat "$ERR")"
  index=$((index + 1))
done
[ "$(grep -c '^entry ' "$CFG")" = 24 ] || fail '6b: did not reach 24 entries'
if entry set "$D" --id e25 --title 'Entry 25' --image boot25.efi --role other; then
  fail '6b: accepted a 25th entry'
fi
grep -q 'already holds 24 entries' "$ERR" || fail '6b: wrong diagnosis for a full menu'
[ "$(grep -c '^entry ' "$CFG")" = 24 ] || fail '6b: entry count changed on refusal'
pass 'the 25th entry is refused rather than silently dropped'

# ------------------------------------------------------------------ case 7 --
root c7
printf 'version 1\ngeneration 4\ndefault ghost\nmode 1\ndevinfo-repair asneeded\n\n' > "$CFG"
printf 'entry keep\n  title Keeper\n  image keep.efi\n  mode 1\n  role other\n\n' >> "$CFG"
printf 'entry noimage\n  title No image\n  role other\n\n' >> "$CFG"
printf 'entry keep\n  title Duplicate\n  image dup.efi\n  role other\n\n' >> "$CFG"
printf 'entry bad id\n  title Unusable\n  image bad.efi\n  role other\n\n' >> "$CFG"
entry set "$D" --id android-a --title 'A' --image boot.efi --role active --default ||
  fail "7: refused a repairable config: $(cat "$ERR")"
[ "$(entry_field "$CFG" keep image)" = keep.efi ] || fail '7: usable entry lost'
grep -q '^entry noimage$' "$CFG" && fail '7: kept an entry with no image'
[ "$(grep -c '^entry keep$' "$CFG")" = 1 ] || fail '7: duplicate id survived'
grep -q 'bad id' "$CFG" && fail '7: kept an unusable id'
[ "$(value "$CFG" default)" = android-a ] || fail '7: stale default not repaired'
grep -q '^canoe-entry: dropped ' "$ERR" || fail '7: dropped entries were not reported'
pass 'a lenient read drops what the BDS would reject, and still writes'

# ------------------------------------------------------------------ case 8 --
root c8
entry set "$D" --id android-a --title 'A' --image boot.efi --role active --default ||
  fail "8: refused: $(cat "$ERR")"
snapshot=$(cat "$CFG")
entry show "$D" || fail "8: show refused: $(cat "$ERR")"
[ "$(cat "$CFG")" = "$snapshot" ] || fail '8: show rewrote canoe.cfg'
[ "$(cat "$OUT")" = "$snapshot" ] || fail '8: show did not print the document'
[ "$(temps "$D")" = 0 ] || fail '8: show left temp files'
pass 'show writes nothing and does not bump the generation'

# ------------------------------------------------------------------ case 9 --
# `mode` exists so a caller that only wants to re-mode a row cannot create one,
# or silently restate its role, by getting an argument wrong.
root c9
entry set "$D" --id android-a --title 'A' --image boot.efi --role active --mode 2 --default ||
  fail "9: refused: $(cat "$ERR")"
printf '\nentry lineage\n  title LineageOS\n  image roms/lineage.efi\n  mode 2\n  role other\n' >> "$CFG"
generation=$(value "$CFG" generation)
entry mode "$D" --id android-a --mode 1 || fail "9: mode refused: $(cat "$ERR")"
[ "$(entry_field "$CFG" android-a mode)" = 1 ] || fail '9: mode was not applied'
[ "$(entry_field "$CFG" android-a role)" = active ] || fail '9: role was rewritten'
[ "$(entry_field "$CFG" android-a image)" = boot.efi ] || fail '9: image was rewritten'
[ "$(entry_field "$CFG" lineage mode)" = 2 ] || fail '9: another entry was re-moded'
[ "$(value "$CFG" generation)" -gt "$generation" ] || fail '9: generation did not advance'
grep -q 'CANOE-MARK: entry-mode-set id=android-a mode=1' "$OUT" || fail '9: no mode receipt'
if entry mode "$D" --id ghost --mode 1; then fail '9: re-moded an entry that does not exist'; fi
if entry mode "$D" --id android-a --mode 7; then fail '9: accepted an invalid mode'; fi
[ "$(grep -c '^entry ' "$CFG")" = 2 ] || fail '9: a refusal changed the entry count'
[ "$(temps "$D")" = 0 ] || fail '9: temp files left behind'
pass 'mode re-modes an existing entry only, and refuses anything else'

echo "all canoe boot-entry fixtures passed"
