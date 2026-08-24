#!/bin/sh
# Coverage for the Windows canoe_stage.bat driver.
# Run from the repository root with:
#   sh targets/toolkit_windows/tests/test_canoe_stage_bat.sh
#
# Two shipped releases in a row were broken by cmd.exe PARSING, not by logic:
# LF-only batch files drifted cmd's label scan, and a caret written inside a
# double-quoted adb argument was passed through verbatim, so the device ran
# `wc -c ^` with stdin redirected and printed nothing at all - reported to the
# user as `gm2p did not land as 120 bytes (got  =)`, the ` =` being what
# `%VAR: =%` expands to when VAR is undefined. So this file checks parsing:
#
#   L1  no caret-escaped redirection inside a double-quoted string
#   L2  no variable that is cleared and then normalized with `%VAR: =%`
#   A   --mode 2 runs green end to end and the device sees an unescaped probe
#   B   a short push is caught and the transaction never starts
#   C   a probe that prints nothing reports a sentinel, never ` =`
#   D   --skip-bds installs the tree and never reads the efisp geometry
#
# A-D need `wine` and `x86_64-w64-mingw32-gcc` (the stub adb.exe/findstr.exe are
# compiled here); they are skipped, loudly, when either is missing. L1-L2 always
# run - they are the cheap net for the class of bug that keeps shipping.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/canoe-stage-bat.XXXXXX")
trap 'rm -rf "$TMP"' EXIT INT TERM HUP

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "ok - $*"; }
skip() { echo "SKIP - $*"; }

RES="$ROOT/targets/toolkit_windows/resources"

# ------------------------------------------------------------------ L1, L2 ---
# A caret is an escape character ONLY outside double quotes. Inside them cmd
# hands it to the child verbatim, which is how `"wc -c ^< %~1"` became a `wc`
# operand on the device instead of a redirection.
for bat in "$RES"/*.bat; do
  awk '
    {
      inq = 0
      for (i = 1; i <= length($0); i++) {
        c = substr($0, i, 1)
        if (c == "\"") { inq = !inq; continue }
        if (inq && c == "^") {
          nc = substr($0, i + 1, 1)
          if (nc == "<" || nc == ">" || nc == "|" || nc == "&") {
            printf "%s:%d: caret-escaped %s inside double quotes\n", FILENAME, FNR, nc > "/dev/stderr"
            bad = 1
          }
        }
      }
    }
    END { exit bad ? 1 : 0 }
  ' "$bat" || fail "L1 $(basename "$bat"): a caret inside quotes is passed through, not consumed"
done
pass "L1 no caret-escaped redirection inside a quoted string"

# `set "VAR="` undefines VAR, and `%VAR: =%` on an undefined variable expands to
# the literal ` =` - the nonsense value the shipped size check reported.
for bat in "$RES"/*.bat; do
  awk '
    {
      if (match($0, /set "[A-Za-z_][A-Za-z0-9_]*="[ \t]*$/)) {
        s = substr($0, RSTART + 5); cleared[substr(s, 1, index(s, "=") - 1)] = FNR
      }
      if (match($0, /set "[A-Za-z_][A-Za-z0-9_]*=%[A-Za-z_][A-Za-z0-9_]*: =%"/)) {
        s = substr($0, RSTART + 5); v = substr(s, 1, index(s, "=") - 1)
        if (v in cleared) {
          printf "%s:%d: %s is cleared at line %d, so %%%s: =%% yields \" =\"\n", \
            FILENAME, FNR, v, cleared[v], v > "/dev/stderr"
          bad = 1
        }
      }
    }
    END { exit bad ? 1 : 0 }
  ' "$bat" || fail "L2 $(basename "$bat"): give the variable a sentinel instead of clearing it"
done
pass "L2 no cleared variable is normalized with %VAR: =%"

# --------------------------------------------------------------- wine setup ---
command -v wine >/dev/null 2>&1 || { skip "wine is not installed; A-D not run"; exit 0; }
MINGW=x86_64-w64-mingw32-gcc
command -v "$MINGW" >/dev/null 2>&1 || { skip "$MINGW is not installed; A-D not run"; exit 0; }

# Stub adb: emulates the transport operations plus the WORD SPLITTING a device
# shell does, so a stray escape character fails the way toybox does - bogus
# operand to stderr, nothing on stdout.
cat > "$TMP/adb.c" <<'EOS'
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

static long fsize(const char *p) { struct stat st; return stat(p, &st) ? -1 : (long)st.st_size; }

static int sh_wc(char *cmd) {
    char *redir = strchr(cmd, '<'), *target = NULL, *operand = NULL;
    if (redir) { *redir = 0; target = strtok(redir + 1, " \t"); }
    for (char *w = strtok(cmd, " \t"); w; w = strtok(NULL, " \t")) {
        if (!strcmp(w, "wc") || !strcmp(w, "-c")) continue;
        operand = w; break;
    }
    if (operand) { fprintf(stderr, "wc: %s: No such file or directory\n", operand); return 1; }
    if (!target) { fprintf(stderr, "wc: no input\n"); return 1; }
    const char *mode = getenv("CANOE_STUB_WC");
    if (mode && !strcmp(mode, "empty")) return 1;
    if (mode && !strcmp(mode, "short") && strstr(target, "gm2p")) { puts("119"); return 0; }
    const char *local = NULL;
    if (strstr(target, "boot.efi.gm2p"))       local = "efisp/boot.efi.gm2p";
    else if (strstr(target, "boot.efi.tzmap")) local = "efisp/boot.efi.tzmap";
    else if (strstr(target, "BDS.efi"))        local = "BDS.efi";
    else if (strstr(target, "boot.efi"))       local = "efisp/boot.efi";
    long n = local ? fsize(local) : -1;
    if (n < 0) { fprintf(stderr, "wc: %s: No such file or directory\n", target); return 1; }
    printf("%ld\n", n);
    return 0;
}

int main(int argc, char **argv) {
    FILE *log = fopen("adb.log", "a");
    if (log) { for (int i = 1; i < argc; i++) fprintf(log, "%s%s", argv[i], i + 1 == argc ? "\n" : " | "); fclose(log); }
    if (argc < 2) return 1;
    int i = 1;
    if (!strcmp(argv[i], "-s") && argc > i + 1) i += 2;
    const char *op = argv[i];
    if (!strcmp(op, "get-state")) { puts("recovery"); return 0; }
    if (!strcmp(op, "push")) return 0;
    if (!strcmp(op, "pull")) {
        if (argc > i + 2) { FILE *f = fopen(argv[i + 2], "wb"); if (f) { fputs("efisp-backup", f); fclose(f); } }
        return 0;
    }
    if (strcmp(op, "shell") || argc <= i + 1) return 0;
    char *cmd = strdup(argv[i + 1]);
    if (strstr(cmd, "wc ")) return sh_wc(cmd);
    if (strstr(cmd, "blockdev --getsize64")) { puts("8388608"); return 0; }
    if (strstr(cmd, "mode-read")) { puts("MODE=2|MODE_DEFAULTED=0"); return 0; }
    if (strstr(cmd, "canoe_device_install.sh")) {
        puts("canoe-device: CANOE-MARK: staged-set-validated gm2p=120 tzmap=256");
        puts("canoe-device: commit complete");
        return 0;
    }
    return 0;
}
EOS

# wine ships findstr as a usage-only stub, so supply the documented behaviour of
# the two forms the toolkit uses: /C:<literal> and the /r subset `^[0-9][0-9]*$`.
cat > "$TMP/findstr.c" <<'EOS'
#include <stdio.h>
#include <string.h>

static int cls(const char *c, size_t n, char ch) {
    for (size_t i = 0; i < n; i++) {
        if (i + 2 < n && c[i + 1] == '-') { if (ch >= c[i] && ch <= c[i + 2]) return 1; i += 2; }
        else if (c[i] == ch) return 1;
    }
    return 0;
}
static int rx(const char *p, const char *s) {
    if (!*p) return 1;
    if (*p == '$' && !p[1]) return *s == 0;
    size_t ilen = 1;
    if (*p == '[') { const char *e = strchr(p, ']'); if (!e) return 0; ilen = (size_t)(e - p) + 1; }
    const char *next = p + ilen;
    int star = (*next == '*');
    const char *rest = star ? next + 1 : next;
    for (const char *q = s;; q++) {
        if (star && rx(rest, q)) return 1;
        char c = *q;
        int ok = c && (*p == '[' ? cls(p + 1, ilen - 2, c) : (*p == '.' || *p == c));
        if (!ok) return 0;
        if (!star) return rx(rest, q + 1);
    }
}
static int search(const char *pat, const char *line) {
    if (*pat == '^') return rx(pat + 1, line);
    for (const char *s = line;; s++) { if (rx(pat, s)) return 1; if (!*s) return 0; }
}
int main(int argc, char **argv) {
    const char *lit = NULL, *re = NULL; int use_re = 0;
    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "/C:", 3)) lit = argv[i] + 3;
        else if (!strcmp(argv[i], "/r") || !strcmp(argv[i], "/R")) use_re = 1;
        else if (argv[i][0] != '/') { if (use_re) re = argv[i]; else lit = argv[i]; }
    }
    if (!lit && !re) { puts("Usage: findstr /options string"); return 2; }
    char line[8192]; int found = 0;
    while (fgets(line, sizeof line, stdin)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (re ? search(re, line) : strstr(line, lit) != NULL) { found = 1; puts(line); }
    }
    return found ? 0 : 1;
}
EOS

"$MINGW" -O1 -o "$TMP/adb.exe" "$TMP/adb.c" || fail "could not build the stub adb.exe"
"$MINGW" -O1 -o "$TMP/findstr.exe" "$TMP/findstr.c" || fail "could not build the stub findstr.exe"

TK="$TMP/tk"
make_toolkit() {
  rm -rf "$TK"
  mkdir -p "$TK/efisp/tools" "$TK/bin"
  # The release zip materializes CRLF (targets/toolkit_windows/.gitattributes);
  # reproduce that here whatever the checkout holds.
  sed 's/$/\r/' "$RES/canoe_stage.bat" | sed 's/\r\r$/\r/' > "$TK/canoe_stage.bat"
  : > "$TK/efisp/boot.efi";       dd if=/dev/zero of="$TK/efisp/boot.efi" bs=1024 count=760 2>/dev/null
  dd if=/dev/zero of="$TK/efisp/boot.efi.gm2p"  bs=1 count=120 2>/dev/null
  dd if=/dev/zero of="$TK/efisp/boot.efi.tzmap" bs=1 count=256 2>/dev/null
  printf 'ENTRY\n' > "$TK/efisp/BOOTENTRIES"
  for t in ArbTools.efi BLTools.efi RebootTools.efi ENTRIES; do printf 'TOOL\n' > "$TK/efisp/tools/$t"; done
  dd if=/dev/zero of="$TK/BDS.efi" bs=1024 count=316 2>/dev/null
  printf '#!/bin/sh\n' > "$TK/canoe_device_install.sh"
  printf 'ELF\n' > "$TK/bin/mode2_profile-arm64"
  cp "$TMP/adb.exe" "$TMP/findstr.exe" "$TK/"
}

# Runs the driver under wine; wine's own diagnostics are noise, the driver's
# errors go to stderr, so both streams are kept and greppable.
run_stage() {
  ( cd "$TK" && WINEDEBUG=-all wine cmd /c canoe_stage.bat "$@" >"$TMP/out" 2>"$TMP/err" ) && echo 0 || echo $?
}
out_has()  { grep -qF "$1" "$TMP/out" || fail "$2 (missing from stdout: $1)"; }
err_has()  { grep -qF "$1" "$TMP/err" || fail "$2 (missing from stderr: $1)"; }
err_lacks() { ! grep -qF "$1" "$TMP/err" || fail "$2 (unexpected in stderr: $1)"; }
log_has()  { grep -qF "$1" "$TK/adb.log" || fail "$2 (device never saw: $1)"; }
log_lacks() { ! grep -qF "$1" "$TK/adb.log" || fail "$2 (device saw: $1)"; }

# --------------------------------------------------------------------- A ------
make_toolkit
rc=$(run_stage --mode 2)
[ "$rc" = 0 ] || fail "A --mode 2 exited $rc"
out_has "staged set validated on device" "A the staged set was not validated"
out_has "record reread: MODE=2|MODE_DEFAULTED=0" "A the mode record reread was rejected"
out_has "canoe_stage: done." "A the run did not complete"
log_has "wc -c < /persist/efisp/.canoe.stage/boot.efi.gm2p" "A the size probe is not a plain redirection"
log_lacks "wc -c ^<" "A the size probe still carries a caret"
log_has "sh /persist/efisp/.canoe.stage/canoe_device_install.sh" "A the transaction never ran"
pass "A --mode 2 stages, installs and sets the preferred mode"

# --------------------------------------------------------------------- B ------
make_toolkit
CANOE_STUB_WC=short; export CANOE_STUB_WC
rc=$(run_stage --mode 2)
unset CANOE_STUB_WC
[ "$rc" = 1 ] || fail "B a short push exited $rc"
err_has "gm2p did not land as 120 bytes (got 119)" "B the short push was not reported with its size"
log_lacks "sh /persist/efisp/.canoe.stage/canoe_device_install.sh" "B the transaction ran after a short push"
log_has "rm -rf /persist/efisp/.canoe.stage" "B the staging directory was left behind"
pass "B a short push aborts before the transaction and names the size"

# --------------------------------------------------------------------- C ------
make_toolkit
CANOE_STUB_WC=empty; export CANOE_STUB_WC
rc=$(run_stage --mode 2)
unset CANOE_STUB_WC
[ "$rc" = 1 ] || fail "C a silent probe exited $rc"
err_has "gm2p did not land as 120 bytes (got none)" "C a silent probe did not report the sentinel"
err_lacks "(got  =)" "C a silent probe still reports the undefined-variable artifact"
pass "C a probe that prints nothing reports a sentinel, not \" =\""

# --------------------------------------------------------------------- D ------
make_toolkit
rc=$(run_stage --skip-bds)
[ "$rc" = 0 ] || fail "D --skip-bds exited $rc"
out_has "staged set validated on device" "D the staged set was not validated"
out_has "canoe_stage: done." "D the run did not complete"
log_lacks "blockdev --getsize64" "D --skip-bds read the efisp geometry anyway"
log_lacks "BDS.efi" "D --skip-bds pushed BDS.efi"
pass "D --skip-bds installs the tree and leaves efisp alone"

echo "all canoe_stage.bat checks passed"
