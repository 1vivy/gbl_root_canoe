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

`canoe install` names this remediation when its wait times out *and* the
session came up on the stock identity.

`canoe install` finds the LUN by its USB identity (`1209:ca0e`, or `05c6:f000`
when the resident fallback served the session) rather than by watching for a
new disk name. A run that timed out or was interrupted can therefore be
repeated against the same live session: it adopts the disk already on the bus
instead of asking the BDS for a second export, which the BDS would not answer
while it sits in its export loop.

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
