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
session as one USB LUN. Finish every write and unmount the filesystem before
ending the session.

**Volume Down on the device ends the session for both paths, including the
fastboot oem path.** Disconnecting or losing the USB link does not cancel it;
reconnect and finish the unmount, then press Volume Down on the device.

Older BDS builds started the oem export without drawing a screen and silently
swallowed keypresses. If the screen does not change, the running BDS predates
this fix.

## Host install through the export

On Linux, let `canoe install` perform the export and mount, or pass an already
mounted boot root:

```bash
canoe install --slot a --mode 1
canoe install --boot-root /path/to/persist/efisp --slot a --mode 1
```

The host installer commits only inside the mounted `persist/efisp` directory.
It never flashes a partition. The operator separately owns the vulnerable ABL
and `BDS.efi` fastboot commands described in [`install.md`](./install.md).

## Windows mount and install

The Windows package bundles `fastboot.exe`, `ext4windows.exe`,
`winfsp-x64.dll`, and the WinFsp installer. Ext4Windows defaults to read-only;
`--rw` is required for the boot-root transaction.

1. Start `fastboot oem mass-storage:persist` or select **USB Mass Storage** in
   the BDS menu and acknowledge the warning.
2. In an elevated PowerShell, record the USB disks before and after the export:

   ```powershell
   Get-Disk | Where-Object BusType -eq 'USB' | Select-Object -ExpandProperty Number
   ```

   Choose the newly enumerated physical number, never a disk that was already
   present before the export.
3. Run `ext4windows.exe status` and choose the first free drive letter from
   `Z:` downward.
4. Mount the exported disk read-write:

   ```text
   ext4windows.exe mount \\.\PhysicalDrive<N> Z: --rw
   ```

5. Install against the boot root:

   ```text
   canoe.cmd install --boot-root Z:\efisp --slot a --mode 1
   ```

6. Flush the installation and unmount the volume:

   ```text
   ext4windows.exe unmount Z:
   ```

7. Press **Volume Down on the device**. This is the only session-cancellation
   control; unmounting alone does not end the BDS export.

If the bundled mount fails, the recovery is exact: run
`ext4windows.exe --scan`, mount the volume manually, then re-run
`canoe.cmd install --boot-root <drive>:\efisp --slot a --mode 1`.

For the configuration format, see the normative
[`canoe.cfg` contract](./canoe-cfg.md).