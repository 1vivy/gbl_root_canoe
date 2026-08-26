# canoe-host — the host-side toolkit

One implementation of the PC-side tools, copied into both toolkit archives by
their Makefiles. Same convention as `tools/canoe-device/`: the source of truth
lives here once, and `targets/toolkit_{linux,windows}/Makefile` copy it in.

| Path | Role |
|---|---|
| `canoe` | Linux launcher and interactive wizard |
| `canoe.cmd` | Windows wrapper; uses bundled Python when available |
| `canoelib/` | Shared implementation package |
| `tests/` | pytest suite; never packaged |

## Why Python

The host drivers were maintained twice, once in bash and once in cmd.exe batch.
The batch half kept breaking on *parsing* rather than on logic — a `^` written
inside a quoted adb argument reaches the device verbatim, so `wc -c ^< file` ran
`wc` with a bogus operand and printed nothing at all, and the empty result was
then reported to the user as the literal string ` =`, which is what
`%VAR: =%` expands to for an undefined variable. Two releases shipped with that
class of bug, and catching it needed wine plus a hand-compiled `findstr.exe`.
One implementation, driven by real tests, removes the class.

## Constraints

- **Stdlib only.** The Windows archive ships an embeddable CPython under
  `python/` with no pip and no site-packages, so a third-party import here would
  break the shipped toolkit rather than just the build. Python 3.11 is the floor.
- **No shell strings.** Every child process is an argv list through
  `canoe.proc.run`, which is what retires the quoting and word-splitting class of
  bug for good.
- The install transaction is deliberately *not* here. It runs through the shared
  `tools/canoe-device/canoe_device_install.sh` and `canoe_boot_entry.sh` scripts,
  so ADB and USB Mass Storage invoke exactly the same implementation.
- `canoe install` defaults to ADB. For a BDS `oem mass-storage:persist` export,
  use `canoe install --via mass-storage`; for an already-mounted persist export,
  use `canoe install --boot-root <drive>:\`. Host-run transactions require
  `--slot a|b`, unless `canoe prep-device` recorded the source slot.

## Development

```sh
python3 -m pytest tools/canoe-host          # from the repository root
cd tools/canoe-host && ruff check . && ruff format --check . && basedpyright
```
