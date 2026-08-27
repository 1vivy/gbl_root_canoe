# canoe-host — the Python host toolkit

This directory is the source of the host implementation copied into the Linux
and Windows toolkit archives.

| Path | Role |
| --- | --- |
| `canoe` | Linux launcher and interactive questionnaire |
| `canoe.cmd` | Windows launcher using the bundled interpreter |
| `canoelib/` | Python implementation |
| `tests/` | Host-only test suite; never packaged |

## Design

The host uses Python 3.11 and the standard library only. Child processes receive
argv lists through `canoe.proc.run`; the host never invokes a shell. Linux and
Windows therefore use the same argument handling and transaction semantics.

The boot-root transaction is owned by `canoelib.boottree`. It validates the
staged `boot.efi` triplet, compares the profile signer digest with the live
generation, snapshots the files it may replace, demotes the live triplet to
`boot_backup.efi`, commits the new tree, writes `canoe.cfg`, and rolls back the
snapshot on failure. `canoelib.config` is the canonical configuration model.

The host reaches an unmounted boot root through the BDS
`fastboot oem mass-storage:persist` export. It can also install against an
already mounted `persist/efisp` path with `--boot-root`. The host transaction
mutates only that directory; the operator separately flashes the vulnerable ABL
and raw `BDS.efi` with fastboot.

## Command surface

```text
canoe
canoe build [--abl IMG] [--vbmeta IMG]
canoe install [--boot-root PATH] --slot a|b [--mode 0|1|2] \
              [--vendor-boot IMG] [--allow-new-signer]
```

With no arguments, `canoe` runs the interactive five-scenario questionnaire.
`build` defaults to `images/abl.img` and `images/vbmeta.img`; explicit image
arguments are copied into those canonical locations before derivation.
`install` requires the slot because the BDS does not publish a current-slot
variable. `--vendor-boot` writes a patched copy in the work area and reports its
fastboot flash command; it does not alter the source image.

The Windows archive bundles `fastboot.exe`, Ext4Windows, WinFsp, and an
embeddable Python interpreter. Ext4Windows is read-only unless invoked with
`--rw`:

```text
ext4windows.exe mount \\.\PhysicalDrive<N> Z: --rw
```

If automatic mounting fails, run `ext4windows.exe --scan`, mount the volume
manually, and rerun `canoe.cmd install --boot-root <drive>:\efisp` with the
required slot and mode.

## Development

The package is developed from the repository root. Scoped checks for this
package are documented by the project maintainers; the release gate runs them
after all package changes are integrated.
