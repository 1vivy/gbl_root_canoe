# canoe-host — the Python host toolkit

This directory is the source of the host implementation copied into the Linux
and Windows toolkit archives.

| Path | Role |
| --- | --- |
| `canoe` | Linux launcher and interactive questionnaire |
| `canoe.cmd` | Windows launcher using the bundled interpreter |
| `canoelib/` | Python transport and process adapters |
| `tests/` | Host-only test suite; never packaged |

## Design

The host uses Python 3.11 and the standard library only. Child processes receive
argv lists through `canoe.proc.run`; the host never invokes a shell. Linux and
Windows therefore use the same argument handling and transaction semantics.

`canoe-bootmgr` is the single boot-root transaction, configuration writer, and
source detector. The host adapter invokes `canoe-bootmgr source detect --json`,
selects a supported unmounted source, and forwards the canonical request.
For a USB export, `canoe-bootmgr` receives the raw block-device source and its
`canoe-ext4` backend performs journal recovery, locking, write-back, and close.
The local `--boot-root` directory form remains available for tests and for an
operator who has already mounted persist outside Canoe.

The operator separately flashes the vulnerable ABL and raw `BDS.efi` with
fastboot. The host installer never writes either partition.

## Command surface

```text
canoe
canoe build [--abl IMG] [--vbmeta IMG]
canoe install [--boot-root PATH] --slot a|b [--mode 0|1|2] ...
canoe entry set|remove|mode ...
canoe config set-policy ...
canoe default get|set ...
canoe bls list|show|stage ...
canoe source detect ...
canoe slot status ...
```

With no arguments, `canoe` runs the interactive questionnaire. `build` defaults
to `images/abl.img` and `images/vbmeta.img`; explicit image arguments are
copied into those canonical locations before derivation. `install` requires the
boot manager, which applies the slot safety rules. When `--boot-root` is absent,
`canoe` starts `fastboot oem mass-storage:persist`, asks `canoe-bootmgr source
detect --json` for candidates, and passes the supported unmounted block device
directly to the boot manager.

Entry, default, BLS, and slot commands are thin routes to the bundled
`canoe-bootmgr` human CLI. They do not implement a second config grammar or
serializer. Use the boot manager's `--json` mode for bounded machine output.

The Windows archive bundles `canoe-bootmgr.exe`, `canoe-ext4.exe`,
`fastboot.exe`, and an embeddable Python interpreter. `canoe-ext4.exe` operates
on `\\.\PhysicalDrive<N>` directly; it does not mount a drive letter. A Windows
helper is required at package-build time. If the native helper cannot be built
on the current host, provide the output of `tools/canoe-ext4/build-windows.sh`
(or set the package input override); packaging fails loudly when it is absent.

## Development

The package is developed from the repository root. Scoped checks for this
package are documented by the project maintainers; the release gate runs them
after all package changes are integrated.
