# USB Mass Storage

BDS exports storage over USB with three targets: `boot-root`, `persist` and
`logfs`. The BDS USB menu lists whichever currently resolve. `boot-root` is the
mounted view of `persist/efisp.fat`, and paths there start at the FAT root, not
at `efisp/`.

## Managed export

The app drives this mode. It enumerates as a vendor-class device (`1209:ca0f`)
with interface-scoped WinUSB descriptors, so no OS filesystem driver claims it
and the app is the exclusive writer. There is nothing to mount, eject or race
against, and no ext4 driver to install. If the managed driver cannot start, the
export fails rather than falling back to an OS-mountable disk.

The app binds the export to the selected device, verifies the container
identity, preserves unrelated files, refuses conflicting managed-file changes,
and flushes before release.

## Manual export

To edit outside the app, pick a target in the BDS USB menu or use Super
Fastboot:

```sh
fastboot oem mass-storage:boot-root
```

This enumerates as ordinary mass storage (`1209:ca0e`) and your OS mounts it: a
drive letter on Windows, a vfat mount point on Linux. Run
`canoe-bootmgr --boot-root <mount> ...` or edit files directly, then eject or
unmount safely before returning to fastboot or booting Android. Here you are one
writer among several, so do not edit the same boot root from another program at
the same time.

## Persist

Raw `persist` export is for provisioning or removing the container and for
deliberate offline ext4 work; routine entry and configuration edits belong on
the FAT boot root. Do not mount or write it while Android is using persist, and
do not write raw FAT sectors while the filesystem is mounted. The app releases
mass storage before fastboot operations and reacquires the right export when
later cleanup requires it.

## Pull logs after a failed reboot

Return to the CANOE-BDS boot menu, open **USB Mass Storage**, and choose
**Export logfs**. Connect the phone to a host and copy the files from the
removable drive. Safely eject or unmount it before leaving the export.

This is the preferred bug-report path after a failed boot. Send the logfs files
with the device model, exact firmware/region, selected Canoe mode and a short
description of what appeared on screen. No Android, recovery mount command or
ADB pull is required.
