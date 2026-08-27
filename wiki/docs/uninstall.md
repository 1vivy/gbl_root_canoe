# Uninstall Guide

## 1. Back up data

Back up important data before removing the chain. Unlock state, recovery
behavior, and data access are device-specific.

## 2. Remove the boot-root configuration

Use either of these routes:

- boot into a recovery that can mount `persist`, then remove
  `/persist/efisp/canoe.cfg`; or
- enter BDS **USB Mass Storage**, export `persist`, mount it on the computer,
  remove `efisp/canoe.cfg`, flush the write, and unmount it.

On a running Android system the same file is
`/mnt/vendor/persist/efisp/canoe.cfg`. Removing the file prevents BDS from
using the configured managed entries; it does not erase the `persist` filesystem.

When the edit is complete, press **Volume Down on the device** to end the BDS
mass-storage session. That is the only session-cancellation control.

## 3. Erase BDS

Boot official fastboot and erase the raw BDS partition:

```bash
fastboot erase efisp
```

This removes `BDS.efi` but does not format or replace `persist`. If official
fastboot is unavailable and BDS is still running, use the BDS fastboot service:

```bash
fastboot flashing lock       # only if hardware re-lock is intended
fastboot erase efisp
```

Keep this order when hardware re-locking: complete the lock operation while the
chain is still present, then erase `efisp`. A real hardware re-lock may require
the vendor's account or device-specific unlock procedure.

## 4. Optional data wipe

If the device-specific uninstall procedure requires a clean data partition:

```bash
fastboot -w
```

This destroys user data; verify the backup first.

## Result

After `canoe.cfg` and raw `efisp` are removed, the BDS chain is no longer
available. The device follows the remaining vendor software and its actual
bootloader state.
