# Uninstall guide

Canoe has no single protocol operation or `canoe` subcommand for a complete
chain removal. The app can remove individual persisted rows through **Boot
entries** (`entry.remove`), but a clean chain removal still requires deleting
`canoe.cfg` and erasing the raw `efisp` partition with the platform fastboot
tool. Do not treat removing one row as an uninstall.

## 1. Back up data

Back up important data before removing the chain. Unlock state, recovery
behavior, and data access are device-specific. If you need to inspect the
current configuration first, use the app's **Boot entries** and **Settings**
views or read it through `config.show`; do not infer an absent row to mean an
empty boot root.

## 2. Remove the boot-root configuration

Use one of these controlled routes:

- boot into a recovery that can mount `persist`, then remove
  `/persist/efisp/canoe.cfg`; or
- start a BDS **USB Mass Storage** export from the app or CLI, stop Android's
  use of `persist`, and use the shipped `canoe-ext4` helper against the
  unmounted export. Do not mount the live export concurrently with Android.
  End the export with the device's VOL DOWN control after the write is flushed.

The CLI export primitive is:

```bash
canoe-bootmgr fastboot export --target persist
```

For an exported `persist` volume whose raw node is `<RAW_NODE>`, remove only
the configuration file with:

```bash
canoe-ext4 remove <RAW_NODE> /efisp/canoe.cfg
```

The app's export/install path uses `fastboot.export` and
`fastboot.end-export` around its transaction. The selected boot-root file is
`/mnt/vendor/persist/efisp/canoe.cfg` on a running Android system and
`\efisp\canoe.cfg` on the exported volume. Removing the file prevents BDS from
using configured managed entries; it does not erase the `persist` filesystem.

A live export is a mass-storage gadget with no fastboot channel. While it is
active, host fastboot commands cannot reach BDS. **VOL DOWN on the device is the
only contractual export cancellation control.**

## 3. Erase BDS

Boot official fastboot and erase the raw BDS partition with the platform tool:

```bash
fastboot erase efisp
```

The app's `fastboot.flash` operation is an explicit image flash and cannot erase
a partition. `canoe-bootmgr` has no protocol erase operation, so do not look for
an in-app erase button or invent a protocol request.

If official fastboot is unavailable while BDS is still running, the same
external command can use the BDS fastboot service:

```bash
fastboot erase efisp
```

If hardware re-lock is intended, complete it while the chain is still present,
then erase `efisp`:

```bash
fastboot flashing lock
fastboot erase efisp
```

A real hardware re-lock may require the vendor's account or device-specific
unlock procedure. Locking triggers the platform's data-wipe behavior.

## 4. Optional data wipe

If the device-specific uninstall procedure requires a clean data partition:

```bash
fastboot -w
```

This destroys user data; verify the backup first.

## Result

After `canoe.cfg` is removed and raw `efisp` is erased, the BDS chain is no
longer available. The device follows the remaining vendor software and its
actual bootloader state. The app and CLI do not claim success until their
individual `entry.remove`, export, or writer responses succeed; those operations
alone do not replace the final external erase.
