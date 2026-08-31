# USB Mass Storage Guide

Super Fastboot can export one physical partition as a USB disk. The `persist`
partition contains the boot root under `/efisp`; `logfs` is useful for collecting
logs when it exists.

## Start an export

From the BDS menu, choose **USB Mass Storage**, select `persist`, and confirm the
live-filesystem warning. The same operation can be started from fastboot:

```bash
fastboot oem mass-storage             # persist (default)
fastboot oem mass-storage:persist     # persist
fastboot oem mass-storage:logfs       # logfs
```

The export screen is shown for both the on-device menu path and the
`fastboot oem mass-storage:persist` path. Only one partition is exported per
session as one USB LUN.

**Volume Down on the device ends the session for both paths, including the
fastboot oem path.** Disconnecting or losing the USB link does not cancel it;
reconnect and finish the operation, then press Volume Down on the device.

Older BDS builds started the oem export without drawing a screen and silently
swallowed keypresses. If the screen does not change, the running BDS predates
this fix.

**The export's identity.** The BDS carries its own mass-storage driver and
starts it on demand: the gadget enumerates as **`1209:ca0e`** (product:
`efisp boot root`, fixed disk) instead of the platform's `05c6:f000`. No system
rule claims that identity, so the modeswitch problem below cannot happen to a
bundled-driver export; if that driver cannot start, the platform's own driver
takes over with the stock presentation, and the tooling accepts both
identities.

**Linux: usb_modeswitch, only on the fallback identity.** Nothing claims
`1209:ca0e`, so a bundled-driver export is never touched. When a session falls
back to the platform driver's `05c6:f000`, stock udev rules match that to a
mode-switching 4G modem whose packaged config ejects the device between
`usb-storage` binding and the disk scan. Disable the switch once:

```bash
printf 'DisableSwitching=1\n' | sudo tee /etc/usb_modeswitch.d/05c6:f000
```

`canoe install` asks `canoe-bootmgr source detect --json` for candidates and
uses the first readable, unmounted block row whose identity is `1209:ca0e` or
the compatibility identity `05c6:f000`. This is the only USB source detector;
the Python host does not walk sysfs or query PowerShell. A run that timed out
or was interrupted can therefore be repeated against the same live session:
it adopts the disk already reported by `source detect` instead of asking the
BDS for a second export.

## Host install through the export

The host never mounts the exported filesystem. It passes the selected raw block
device directly to `canoe-bootmgr`, whose libext2fs-backed `canoe-ext4` backend
owns locking, journal recovery, bounded writes, flush, and close:

```bash
canoe install --slot a --mode 1
```

The helper creates `/efisp` when it is missing. The boot manager then commits
all boot-root files, sidecars, configuration, and rollback through the same
backend. The host installer never flashes a partition; the operator separately
owns the vulnerable ABL and `BDS.efi` commands described in [`install.md`](./install.md).

For an ext4 image or raw block source outside a live export, select the direct
backend explicitly:

```bash
canoe-bootmgr --source /path/to/persist.ext4 install \
  --staged /path/to/staged --slot a --mode 1
canoe-bootmgr --ext4-image /path/to/persist.ext4 install \
  --staged /path/to/staged --slot a --mode 1
```

The bilingual `canoe-gui` exposes the same choices with
`--source`/`--ext4-image` or `--boot-root`; see [`install.md`](./install.md)
for the backend comparison.

For tests or an operator-managed local directory, use the explicit local
backend:

```bash
canoe install --boot-root /path/to/persist/efisp --slot a --mode 1
```

## Windows raw-disk install

The Windows package bundles `canoe-bootmgr.exe`, `canoe-ext4.exe`, and
`fastboot.exe`. After selecting the newly enumerated USB physical disk, the
boot manager passes its `\\.\PhysicalDrive<N>` source directly to the helper:

```text
canoe.cmd install --slot a --mode 1
```

No drive-letter filesystem mount or third-party filesystem driver is used.
The package build requires `canoe-ext4.exe`; if a native build is unavailable,
provide the output of `tools/canoe-ext4/build-windows.sh` through the package
input override. Missing input fails the build rather than dropping Windows
support.

Press **Volume Down on the device** after the operation. This is the only
session-cancellation control.

For the configuration format, see the normative
[`canoe.cfg` contract](./canoe-cfg.md).
## Detecting and attaching sources

`canoe-bootmgr source detect --json` is non-mutating and does not require
privilege to enumerate candidates. It reports each source's kind (`block`,
`image`, or `dir`), path, identity, model, size, boot-root presence,
readability, writability, `needs_privilege`, mount point, and explanation:

```json
{"ok":true,"kind":"source.detect","sources":[]}
```

The GUI Connect screen uses the same response and provides one-click attach,
Refresh, and manual directory/image/device selection. Directory and image
sources never need elevation. On access denial, Linux offers **Retry with
pkexec** plus a copyable `sudo` command; Windows offers **Restart as
Administrator**. Neither surface silently escalates.

Windows supports explicit dirty-journal recovery through `canoe-ext4.exe
--recover`; code 4 means the filesystem is dirty and recovery is never
implicit. The Windows helper links the e2fsprogs journal/revoke/recovery
objects required for replay.
