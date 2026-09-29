---
name: edk2-linker-regression-triage
description: "Diagnose EDK2 crashes caused by stale artifacts, wrong branches, or missing dynamic relocations before changing firmware code."
---

# Disprove the linker hypothesis before you rewrite anything

When a UEFI image faults, "the linker is wrong / the blob is stale / we're not
on latest" is a cheap hypothesis to state and an expensive one to act on. All
four checks below run in seconds, need no device, and each one either kills the
hypothesis or names the fix. Run them **first**. A crash that survives all four
is in your own code, and that is where to look next.

This exists because a real session burned effort on the linker theory while the
actual defects were a use-after-free and a missing `ConnectController` pass.

## 1. Is the bundled blob actually stale?

Hash it against the sibling repo's HEAD artifact, and check which commit
introduced that file. Identical hash + the fix commit touching that path means
the blob already carries the fix.

```bash
sha256sum <consumer>/blobs/<blob>.efi <producer>/blobs/<blob>.efi \
          <producer>/pkg/build/<Module>.efi
git -C <producer> log --format='%h %ad %s' --date=format:'%m-%d %H:%M' \
    -1 -- blobs/<blob>.efi
```

## 2. Is the branch actually behind?

Do not trust the branch name. Compare dates AND ancestry — a `feat/x` branch is
often the *superseded* line, not the newer one.

```bash
git log --format='%h %ad %s' --date=format:'%m-%d %H:%M' -1 <candidate> <current>
git merge-base --is-ancestor <candidate> <current> \
  && echo "candidate contained" || echo "candidate NOT contained"
```

Older date + not an ancestor = an abandoned approach. Say so and move on.

## 3. Did the flags actually reach the link?

`CLANG_EXTRA_DLINK_FLAGS` in the Qualcomm EDK2 makefile is **conditional on
clang >= 17**. An older toolchain silently expands it to nothing and every
module links without `--apply-dynamic-relocs`.

```bash
docker run --rm <builder-image> /bin/bash -lc 'clang --version | head -1'
grep -c 'apply-dynamic-relocs' <build-log>     # expect >=1 per linked module
```

## 4. Relocation health on the final PE — the decisive check

Without applied dynamic relocs, lld leaves statically initialised pointers as
zero slots, GenFw copies `.data` verbatim and only rebases, so **every pointer
resolves to the image base**: the module loads, starts, then dies at its first
call through a pointer in its own static data.

`llvm-objcopy --dump-section .data=…` is unreliable on these images — it errors
outright against both the shipped `.efi` and the build tree's `.dll` — so parse
`llvm-objdump` instead. The ELF `.dll` in the build tree yields the same slot
count as the final PE, because GenFw copies `.data` verbatim:

```python
import subprocess, struct, re
def data_slots(path):
    out = subprocess.run(['llvm-objdump','-s','-j','.data',str(path)],
                         capture_output=True, text=True).stdout
    blob = bytearray()
    for line in out.splitlines():
        m = re.match(r'^\s*([0-9a-f]+)\s((?:[0-9a-f]{2,8}\s){1,4})\s', line)
        if m:
            blob += bytes.fromhex(m.group(2).replace(' ', ''))
    return [struct.unpack_from('<Q', blob, i)[0] for i in range(0, len(blob)-7, 8)]

s = data_slots('BDS.efi')
nz  = [x for x in s if x]
ptr = [x for x in nz if 0x1000 <= x < 0x800000]     # plausible in-image pointers
print(len(nz), len(set(nz)), len(ptr), len(set(ptr)))
```

Healthy reference values measured in this tree with that script: canoe `BDS.efi`
163 distinct pointer-like slots, the bundled MSD blobs 84, standalone
AndroidTools 52–101 (`UsbTools` 52, `MdTools` 68, `SurfaceTools` 100). The
absolute count drifts with the code; the decisive signature is a count of
**one**, which means the link is broken. Many distinct values means it is not.

## When all four pass

The linker is exonerated; report that with the numbers rather than re-running
the theory. Then suspect what actually changed on the failing path — most often
newly added code. Two recurring UEFI classes worth checking first:

- **Handle lifetime.** `DisconnectController` destroys child handles. Any
  protocol interface resolved *before* a teardown (a mode switch, a release, a
  reconnect) is dangling afterwards. Fix by passing a *name* and resolving after
  the teardown, so a stale pointer is unrepresentable — not by reordering calls.
- **No connect pass.** Code that "rescans" by walking `LocateHandleBuffer` finds
  only what is already bound. Hot-plugged media needs an explicit
  `ConnectController`/connect-all pass or it is enumerated and never bound.

## Corollary: do not trust the log to survive

Platform firmware often defers its log flush until boot continues into an OS
stage, so precisely the faulting runs lose every mark. Before relying on log
marks for a crash, put the decisive counters **on the screen**. Also beware
mount/probe marks that count only *newly created* handles — an already-bound
volume reports as not-mounted and reads as a failure it is not.
