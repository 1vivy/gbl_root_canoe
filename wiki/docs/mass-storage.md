# USB Mass Storage

Canoe can export one physical partition as one USB disk. The `persist`
partition contains the boot root under `/efisp`; `logfs` is useful for collecting
logs when it exists. The app and `canoe-bootmgr` own the export workflow. Do not
mount or edit the live filesystem from a second tool.

## Before exporting

A live `persist` export is exclusive. Stop Android, or otherwise ensure Android
is not using `persist`, before a host operates on the export. The host must not
use a live `persist` export concurrently with Android. This avoids two writers
using the same ext4 journal and boot-root files.

## Start an export

The BDS menu still has the **USB Mass Storage** action. From the app, the
Guided flow's Commit step requests the export and keeps it held only while the
install transaction runs. The same operation is available to the CLI:

```bash
canoe-bootmgr fastboot export --target persist
canoe-bootmgr fastboot export --target logfs
```

The response identifies the raw block node. Only one partition is exported per
session. `fastboot.export` adopts an existing unmounted Canoe export when one
is already present instead of starting a second export.

An export has two equal, ordinary endings: the host ends it (the boot manager or CLI issues a SCSI eject), or the operator presses **VOL DOWN** on the device. Neither is a failure, and neither is a workaround for the other.

When Canoe's bundled mass-storage driver serves the export, it delivers the SCSI reply before the gadget goes away, and the device returns to the surface it came from—the menu or fastboot. That round trip is the point: a host tool can export `persist`, do its work, and hand the device back without the operator touching the phone.

This depends on Canoe's own bundled driver serving the export. The stock resident platform driver never reports the eject, so a session it serves ends only the old way. On an SM8850 Canoe target, the export enumerates as **`1209:ca0e`** "USB MASS STORAGE", which is Canoe's bundled driver, so the host eject is the normal path there.

Disconnecting or losing the USB link does not cancel the export; reconnect and finish the operation, then use either ordinary ending. While the export runs, the USB link is a mass-storage gadget and carries no fastboot channel, so a fastboot command legitimately reports `waiting-for-any-device`. `fastboot.end-export --node <RAW_NODE>` is available to the CLI for the host-ending path.

Older BDS builds started an export without drawing a screen and silently
swallowed key presses. If the screen does not change, the running BDS predates
that fix.

## Export identity on Linux

Canoe's bundled mass-storage driver normally enumerates as **`1209:ca0e`**
(product `efisp boot root`, fixed disk), rather than the platform's
`05c6:f000`. If the bundled driver cannot start, the platform driver can fall
back to the stock identity; the source detector accepts both.

No system rule claims `1209:ca0e`, so the bundled export is not mode-switched.
On Linux, a fallback `05c6:f000` can match a distribution `usb_modeswitch` rule
for a 4G modem and be ejected before disk scanning. Disable that switch once if
this fallback is used:

```bash
printf 'DisableSwitching=1\n' | sudo tee /etc/usb_modeswitch.d/05c6:f000
```

`source.detect` is the single detector used by the app and CLI. It reports
candidate kind (`block`, `image`, or `dir`), path, identity, model, size,
boot-root presence, readability/writability, privilege need, mount point, and
reason. An unmounted, readable Canoe export is an `export_candidate`. It is
safe to retry a timed-out operation: a later run adopts the disk already
reported by `source.detect` rather than asking BDS for another export.

```bash
canoe source detect --json
canoe-bootmgr --json source detect
```

The native host does not walk sysfs or query PowerShell itself. If access is
denied, the app offers an explicit privilege retry on Linux or an administrator
restart on Windows; it never escalates silently.

## Install through an export

The host must not mount the exported filesystem. `canoe-bootmgr` passes the
selected raw block source to its libext2fs-backed `canoe-ext4` backend, which
owns locking, journal recovery, bounded writes, flush, and close:

```bash
canoe install --slot a --mode 1
```

The `canoe` wrapper asks `source.detect` for a readable, unmounted export and
adopts it. For an explicit source, use the global source selector:

```bash
canoe-bootmgr --source <RAW_NODE> install \
  --staged /path/to/staged --slot a --mode 1
canoe-bootmgr --source /path/to/persist.ext4 install \
  --staged /path/to/staged --slot a --mode 1
canoe-bootmgr --ext4-image /path/to/persist.ext4 install \
  --staged /path/to/staged --slot a --mode 1
```

The backend creates `/efisp` when it is missing and commits boot-root files,
sidecars, configuration, and rollback as one transaction. The host installer
does not flash a partition; the vulnerable ABL and `BDS.efi` operations remain
separate, deliberate fastboot actions in the install guide.

After the transaction, end the export from the host or the device. For the host-ending path:

```bash
canoe-bootmgr fastboot end-export --node <RAW_NODE>
```

Alternatively, press **VOL DOWN** on the device.

If the app reports a held export, finish that app flow rather than starting a
second export from another terminal.

## Windows raw-disk operation

The Windows toolkit includes the native `canoe-bootmgr.exe` and `canoe-ext4.exe`.
After the exported physical disk is detected, the boot manager passes its
`\\.\PhysicalDrive<N>` source directly to the helper. No Python installation,
drive-letter mount, or third-party filesystem driver is needed.

Windows supports explicit dirty-journal recovery through
`canoe-ext4.exe --recover`; exit code 4 means the filesystem is dirty, and
recovery is never implicit. If the helper cannot access the disk, use the app's
explicit **Restart as Administrator** action and retry; do not keep Android
using the live export.

For the configuration format, see the normative
[`canoe.cfg` contract](./canoe-cfg.md). For OTA and mode changes, use the
app's Guided flow so the export, evidence, acknowledgement, write, and end step
remain one reviewed sequence.
