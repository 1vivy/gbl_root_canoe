#!/bin/sh
# canoe_boot_entry.sh - the single writer of canoe.cfg.
#
#   sh canoe_boot_entry.sh set <boot_root> --id ID --title TITLE --image IMAGE \
#          --role active|inactive|backup|other [--mode 0|1|2] [--default] \
#          [--global-mode 0|1|2] [--timeout SECONDS] \
#          [--devinfo-repair asneeded|never]
#   sh canoe_boot_entry.sh mode <boot_root> --id ID --mode 0|1|2
#   sh canoe_boot_entry.sh remove <boot_root> --id ID
#   sh canoe_boot_entry.sh show <boot_root>
#
# `mode` re-modes an entry that already exists and nothing else. It is separate
# from `set` because the callers that only want to change a launch mode - the
# WebUI mode selector, and the signer gate downgrading a Mode 2 row - must not
# be able to create a row or restate its role by getting an argument wrong.
#
# One writer, because there were six: canoe_device_install.sh, the module's
# customize.sh and bin/bl_flasher.sh, its OTA watcher, and the
# host's canoelib/config.py - each with its own idea of which entries survive a
# write. The host even pushed a config the device then threw away, which is how
# `canoe install --mode 0` used to land `mode 1`.
#
# `set` is an UPSERT: the named entry is created or replaced in place and every
# other entry is kept verbatim, so a second install, an OTA-added slot entry and
# a hand-added custom-ROM entry can coexist. That is what makes updating one
# boot entry a normal operation instead of a rewrite of the whole menu.
#
# The emitted document is byte-identical to what canoelib/config.py's
# serialize_config() produces for the same state, so the host can read back what
# the device wrote, re-serialize it and compare.
#
# Reads are lenient and writes are strict: an entry already in the file that the
# BDS parser would reject is dropped with a warning rather than wedging the
# install, but nothing this script emits is ever outside the grammar in
# wiki/docs/canoe-cfg.md.
#
# `show` prints the normalized document and writes nothing; `set` and `remove`
# replace canoe.cfg atomically and bump `generation`.
set -eu
LC_ALL=C
export LC_ALL

die() { printf 'canoe-entry: error: %s\n' "$1" >&2; exit 1; }
mark() { printf 'CANOE-MARK: %s\n' "$1"; }

OP=${1:-}
[ -n "$OP" ] || die "usage: canoe_boot_entry.sh set|remove|show <boot_root> ..."
shift
ROOT=${1:-}
[ -n "$ROOT" ] || die "a boot root is required"
shift

CBE_OP=$OP
CBE_ID=
CBE_TITLE=
CBE_IMAGE=
CBE_MODE=
CBE_ROLE=
CBE_DEFAULT=no
CBE_GLOBAL_MODE=
CBE_TIMEOUT=
CBE_DEVINFO=
CBE_GENFILE=
export CBE_OP CBE_ID CBE_TITLE CBE_IMAGE CBE_MODE CBE_ROLE CBE_DEFAULT
export CBE_GLOBAL_MODE CBE_TIMEOUT CBE_DEVINFO CBE_GENFILE

need() { [ $# -ge 2 ] || die "$1 needs a value"; }

while [ $# -gt 0 ]; do
  case "$1" in
    --id)             need "$@"; CBE_ID=$2; shift 2 ;;
    --title)          need "$@"; CBE_TITLE=$2; shift 2 ;;
    --image)          need "$@"; CBE_IMAGE=$2; shift 2 ;;
    --mode)           need "$@"; CBE_MODE=$2; shift 2 ;;
    --role)           need "$@"; CBE_ROLE=$2; shift 2 ;;
    --global-mode)    need "$@"; CBE_GLOBAL_MODE=$2; shift 2 ;;
    --timeout)        need "$@"; CBE_TIMEOUT=$2; shift 2 ;;
    --devinfo-repair) need "$@"; CBE_DEVINFO=$2; shift 2 ;;
    --default)        CBE_DEFAULT=yes; shift ;;
    *) die "unknown option: $1" ;;
  esac
done

case "$OP" in
  set)
    [ -n "$CBE_ID" ]    || die "set needs --id"
    [ -n "$CBE_TITLE" ] || die "set needs --title"
    [ -n "$CBE_IMAGE" ] || die "set needs --image"
    [ -n "$CBE_ROLE" ]  || die "set needs --role"
    ;;
  mode)
    [ -n "$CBE_ID" ]   || die "mode needs --id"
    [ -n "$CBE_MODE" ] || die "mode needs --mode"
    ;;
  remove) [ -n "$CBE_ID" ] || die "remove needs --id" ;;
  show) ;;
  *) die "unknown operation: $OP" ;;
esac

[ -d "$ROOT" ] || die "boot root not found: $ROOT"
CONFIG="$ROOT/canoe.cfg"
if [ -e "$CONFIG" ]; then SOURCE=$CONFIG; else SOURCE=/dev/null; fi

# The whole document is rendered by one awk program: parse what is there, apply
# the operation, validate, emit. Values arrive through the environment rather
# than -v so that awk never reinterprets a backslash in a title or image path,
# and the resulting generation leaves through CBE_GENFILE rather than sharing
# stderr with the diagnostics.
render() {
  awk '
function warn(msg) { printf("canoe-entry: %s\n", msg) > "/dev/stderr" }
function bad(msg)  { printf("canoe-entry: error: %s\n", msg) > "/dev/stderr"; exit 2 }

function is_number(value) { return value ~ /^[0-9]+$/ }
function valid_id(value)  { return value ~ /^[A-Za-z0-9._-]+$/ && length(value) <= 31 }
function valid_title(value) {
  return length(value) >= 1 && length(value) <= 47 && value !~ /[^ -~]/
}
function valid_role(value) {
  return value == "active" || value == "inactive" || value == "backup" || value == "other"
}

# Mirrors canoelib/config.py _image_value(): fold backslashes, drop one leading
# slash, then reject empty, ".", ".." and doubled separators.
function fold_image(value, path, count, parts, index_) {
  path = value
  gsub(/\\/, "/", path)
  sub(/^\//, "", path)
  if (path == "" || length(path) > 198 || path ~ /[^ -~]/) return ""
  count = split(path, parts, "/")
  for (index_ = 1; index_ <= count; index_++) {
    if (parts[index_] == "" || parts[index_] == "." || parts[index_] == "..") return ""
  }
  return path
}

BEGIN {
  op = ENVIRON["CBE_OP"]
  genfile = ENVIRON["CBE_GENFILE"]
  generation = 0; timeout = 5; global_mode = 1; devinfo = "asneeded"; default_id = ""
  count = 0; current = 0
}

{
  line = $0
  sub(/\r$/, "", line)
  sub(/^[ \t]+/, "", line)
  sub(/[ \t]+$/, "", line)
  if (line == "" || line ~ /^#/) next
  key = line; value = ""
  if (match(line, /[ \t]/)) {
    key = substr(line, 1, RSTART - 1)
    value = substr(line, RSTART + 1)
    sub(/^[ \t]+/, "", value)
    sub(/[ \t]+$/, "", value)
  }
  if (key == "entry") {
    current = 0
    if (!valid_id(value)) { warn("dropped an entry with an unusable id: " value); next }
    if (value in seen) { warn("dropped a duplicate entry: " value); next }
    seen[value] = 1
    count++
    order[count] = value
    current = count
    title[value] = value
    image[value] = ""
    mode[value] = ""
    role[value] = "other"
    next
  }
  # Once an entry has opened, every later key belongs to it. That mirrors the
  # BDS parser, which rejects a global key that appears after the first entry.
  if (current > 0) {
    entry = order[current]
    if (key == "title" && valid_title(value)) title[entry] = value
    else if (key == "image") image[entry] = value
    else if (key == "mode" && value ~ /^[012]$/) mode[entry] = value
    else if (key == "role" && valid_role(value)) role[entry] = value
    next
  }
  if (key == "generation" && is_number(value) && value + 0 <= 4294967295) generation = value + 0
  else if (key == "timeout" && is_number(value) && value + 0 <= 60) timeout = value + 0
  else if (key == "mode" && value ~ /^[012]$/) global_mode = value + 0
  else if (key == "devinfo-repair" && (value == "asneeded" || value == "never")) devinfo = value
  else if (key == "default") default_id = value
}

END {
  # 1. drop what the BDS would refuse to launch anyway
  kept = 0
  for (index_ = 1; index_ <= count; index_++) {
    entry = order[index_]
    folded = fold_image(image[entry])
    if (folded == "") {
      warn("dropped entry " entry ": unusable image path " image[entry])
      delete seen[entry]
      continue
    }
    image[entry] = folded
    kept++
    order[kept] = entry
  }
  for (index_ = kept + 1; index_ <= count; index_++) delete order[index_]
  count = kept

  # 2. apply the operation
  target = ENVIRON["CBE_ID"]
  if (op == "set") {
    if (!valid_id(target)) bad("invalid entry id: " target)
    if (!valid_title(ENVIRON["CBE_TITLE"])) bad("entry title must be 1..47 printable ASCII characters")
    if (!valid_role(ENVIRON["CBE_ROLE"])) bad("invalid entry role: " ENVIRON["CBE_ROLE"])
    folded = fold_image(ENVIRON["CBE_IMAGE"])
    if (folded == "") bad("invalid boot-root-relative image path: " ENVIRON["CBE_IMAGE"])
    if (ENVIRON["CBE_GLOBAL_MODE"] != "") {
      if (ENVIRON["CBE_GLOBAL_MODE"] !~ /^[012]$/) bad("global mode must be 0, 1 or 2")
      global_mode = ENVIRON["CBE_GLOBAL_MODE"] + 0
    }
    if (ENVIRON["CBE_TIMEOUT"] != "") {
      if (!is_number(ENVIRON["CBE_TIMEOUT"]) || ENVIRON["CBE_TIMEOUT"] + 0 > 60) bad("timeout must be in 0..60")
      timeout = ENVIRON["CBE_TIMEOUT"] + 0
    }
    if (ENVIRON["CBE_DEVINFO"] != "") {
      if (ENVIRON["CBE_DEVINFO"] != "asneeded" && ENVIRON["CBE_DEVINFO"] != "never")
        bad("devinfo-repair must be asneeded or never")
      devinfo = ENVIRON["CBE_DEVINFO"]
    }
    # An unspecified mode keeps the mode this entry already carried, and a new
    # entry inherits the file-global fallback. That is what lets an OTA add a
    # slot entry without deciding policy for it.
    new_mode = ENVIRON["CBE_MODE"]
    if (new_mode == "") {
      new_mode = ((target in seen) && mode[target] != "") ? mode[target] : (global_mode "")
    } else if (new_mode !~ /^[012]$/) {
      bad("entry mode must be 0, 1 or 2")
    }
    if (!(target in seen)) {
      if (count >= 24) bad("canoe.cfg already holds 24 entries")
      count++
      order[count] = target
      seen[target] = 1
    }
    title[target] = ENVIRON["CBE_TITLE"]
    image[target] = folded
    mode[target] = new_mode
    role[target] = ENVIRON["CBE_ROLE"]
    if (ENVIRON["CBE_DEFAULT"] == "yes") default_id = target
  } else if (op == "mode") {
    if (!(target in seen)) bad("no such entry: " target)
    if (ENVIRON["CBE_MODE"] !~ /^[012]$/) bad("entry mode must be 0, 1 or 2")
    mode[target] = ENVIRON["CBE_MODE"]
  } else if (op == "remove") {
    if (!(target in seen)) bad("no such entry: " target)
    delete seen[target]
    kept = 0
    for (index_ = 1; index_ <= count; index_++) {
      if (order[index_] == target) continue
      kept++
      order[kept] = order[index_]
    }
    for (index_ = kept + 1; index_ <= count; index_++) delete order[index_]
    count = kept
  }

  if (count == 0) bad("canoe.cfg would have no usable entry")

  # 3. A default naming a departed entry is worse than no default: prefer the
  #    active entry, then the first one, so the menu always has a selection.
  if (!(default_id in seen)) {
    default_id = ""
    for (index_ = 1; index_ <= count && default_id == ""; index_++) {
      if (role[order[index_]] == "active") default_id = order[index_]
    }
    if (default_id == "") default_id = order[1]
  }

  if (op != "show") {
    if (generation >= 4294967295) bad("generation cannot be bumped past 4294967295")
    generation++
  }

  document = "version 1\n"
  document = document "generation " generation "\n"
  document = document "timeout " timeout "\n"
  document = document "default " default_id "\n"
  document = document "mode " global_mode "\n"
  document = document "devinfo-repair " devinfo "\n\n"
  for (index_ = 1; index_ <= count; index_++) {
    entry = order[index_]
    entry_mode = (mode[entry] == "") ? (global_mode "") : mode[entry]
    document = document "entry " entry "\n"
    document = document "  title " title[entry] "\n"
    document = document "  image " image[entry] "\n"
    document = document "  mode " entry_mode "\n"
    document = document "  role " role[entry] "\n\n"
  }
  # The serializer in config.py ends the last entry with a single newline, and
  # the host compares bytes, so the final blank separator comes off here.
  sub(/\n$/, "", document)
  if (length(document) > 8192) bad("canoe.cfg would exceed 8192 bytes")
  printf("%s", document)
  if (genfile != "") printf("%s\n", generation) > genfile
}
' "$1"
}

if [ "$OP" = show ]; then
  render "$SOURCE"
  exit 0
fi

CBE_GENFILE="$ROOT/.canoe.cfg.gen.$$"
TEMP="$ROOT/.canoe.cfg.tmp.$$"
rm -f "$CBE_GENFILE" "$TEMP"

rc=0
render "$SOURCE" >"$TEMP" || rc=$?
if [ "$rc" != 0 ]; then
  rm -f "$TEMP" "$CBE_GENFILE"
  # rc 2 is the renderer's own validation failure; it has already said why.
  [ "$rc" = 2 ] && exit 1
  die "could not render canoe.cfg"
fi
[ -s "$TEMP" ] || { rm -f "$TEMP" "$CBE_GENFILE"; die "generated canoe.cfg is empty"; }
GENERATION=$(cat "$CBE_GENFILE" 2>/dev/null || echo '?')
rm -f "$CBE_GENFILE"

mv -f "$TEMP" "$CONFIG" || { rm -f "$TEMP"; die "could not install $CONFIG"; }
sync || :

case "$OP" in
  set)    mark "entry-set id=$CBE_ID role=$CBE_ROLE mode=${CBE_MODE:-inherited} generation=$GENERATION" ;;
  mode)   mark "entry-mode-set id=$CBE_ID mode=$CBE_MODE generation=$GENERATION" ;;
  remove) mark "entry-removed id=$CBE_ID generation=$GENERATION" ;;
esac
