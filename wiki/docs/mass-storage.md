# USB Mass Storage Guide

Superfastboot can export one physical partition to a connected PC as a normal USB disk. This is useful when ADB does not work: `persist` contains the boot root, while `logfs` can hold the boot log from a device that does not reach Android.

## Enter the export

From the BDS boot menu, choose **USB Mass Storage**. Select one of the offered targets and confirm. `logfs` is shown only when that partition exists.

The same operation is available from fastboot:

```bash
fastboot oem mass-storage             # persist (the default)
fastboot oem mass-storage:persist     # persist
fastboot oem mass-storage:logfs       # logfs
```

Connect the device to the PC before or after selecting the target, then wait for the PC to enumerate the exported disk.

## Targets

- **`persist`** — contains the boot root under `/efisp`, including `canoe.cfg`, the configured entries, and their sidecars. It is the repair channel for a device with no working ADB.
- **`logfs`** — offered only if the partition exists; use it to pull boot logs from a device that will not boot.

Before exporting `persist`, BDS shows a warning because it is a live filesystem. Treat the exported disk as the device's active storage: only make deliberate repairs, and finish writes before ending the session.

Each session exports exactly one partition as one USB LUN. The feature does not expose `persist` and `logfs` simultaneously. Press **Volume Down** on the device to stop the export; unplugging or losing the USB link does **not** end the session, and replugging resumes it. After the host has flushed all writes and unmounted `persist`, the operator must press **Volume Down** to return to the BDS menu.

## Windows mount and repair

The Windows archive bundles `platform-tools`. For ext4 read/write, its Windows path uses **WinFsp plus LKL `lklfuse`**. They are not vendored into the repository: on first use they are fetched and SHA-256-verified, then made available to the Windows helper.

To repair the boot root from Windows:

1. Start the `persist` export and acknowledge the live-filesystem warning.
2. Let Windows enumerate the exported USB disk.
3. Use the fetched, verified WinFsp + LKL `lklfuse` bridge to mount that disk's ext4 filesystem read/write.
4. Edit the boot root at `persist/efisp` (for example, repair `canoe.cfg` or a boot-entry file), flush and unmount it cleanly.
5. Press **Volume Down** on the device to end the export session.

Use only one partition per session. For the configuration syntax, see the normative [`canoe.cfg` contract](./canoe-cfg.md).
