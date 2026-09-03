#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
fixture="$here/fixtures/gpt-slot-reset-dump.img"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/canoe-gpt-l2.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

python3 "$here/gpt_verify.py" "$fixture"
python3 "$here/gpt_reset_copy.py" "$fixture" "$tmp/reset.img"
python3 "$here/gpt_verify.py" "$tmp/reset.img" --compare "$fixture" --target abl_a

python3 "$here/gpt_reset_copy.py" "$tmp/reset.img" "$tmp/no-change.img"
before=$(sha256sum "$tmp/reset.img" | awk '{print $1}')
after=$(sha256sum "$tmp/no-change.img" | awk '{print $1}')
test "$before" = "$after"
printf 'NO-CHANGE byte-identical sha256=%s\n' "$before"

if python3 "$here/gpt_reset_copy.py" "$fixture" "$tmp/retry8.img" --retry 8; then
  echo "retry=8 unexpectedly accepted" >&2; exit 1
fi
python3 - "$fixture" "$tmp/all-ones.img" all-ones <<'PY'
import struct, sys
from pathlib import Path
p=bytearray(Path(sys.argv[1]).read_bytes())
for off in (2*512+48, (256-1-32)*512+48): struct.pack_into('<Q',p,off,(1<<64)-1)
Path(sys.argv[2]).write_bytes(p)
PY
if python3 "$here/gpt_reset_copy.py" "$tmp/all-ones.img" "$tmp/refused.img"; then
  echo "all-ones attributes unexpectedly accepted" >&2; exit 1
fi
python3 - "$fixture" "$tmp/missing.img" <<'PY'
import sys
from pathlib import Path
p=bytearray(Path(sys.argv[1]).read_bytes())
for off in (2*512, (256-1-32)*512): p[off:off+16]=bytes(16)
Path(sys.argv[2]).write_bytes(p)
PY
if python3 "$here/gpt_reset_copy.py" "$tmp/missing.img" "$tmp/refused.img"; then
  echo "missing target unexpectedly accepted" >&2; exit 1
fi
echo "MALFORMED refused retry=8, all-ones attributes, and missing target entry"
echo "L2 offline GPT reset: PASS"
