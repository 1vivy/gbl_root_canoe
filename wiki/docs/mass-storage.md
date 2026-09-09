# USB Mass Storage

BDS can export the **boot root** as ordinary removable FAT storage. This is the
mounted view of `persist/efisp.fat`; Windows cannot see the file while only its
containing ext4 persist partition is exported.

For normal edits outside the GUI, choose the boot-root export in the BDS USB
menu or use the explicit Super Fastboot command:

```sh
fastboot oem mass-storage:boot-root
```

Use the filesystem mounted by your OS. On Windows this may have a drive letter;
on Linux use its vfat mount point. Run `canoe-bootmgr --boot-root <mount> ...`
or edit ordinary files there. Paths start at the FAT root, not at `efisp/`.
Safely eject/unmount before returning to fastboot or booting Android.

The application binds the export to the selected USB device and verifies the
container identity. It preserves unrelated files and OS-created metadata,
refuses conflicting managed-file changes, and flushes the filesystem before
normal dismount and USB release. If another program holds a file open, close it
and retry. This is not a claim of exclusivity against arbitrary external writers.
Do not edit the same boot root in another program while applying a GUI operation.

Raw **persist** export is separate. It is used only for provisioning/removing
the container or deliberate offline ext4 work. Routine entry/configuration edits
use FAT and the native OS filesystem. The Windows adapter uses unchanged
libext2fs for the ext4 allocation/removal boundary; no Windows ext4 driver,
Ext4Windows installation, WinFsp, or patched e2fsprogs is required.

Do not mount or write a raw persist export while Android is using persist.
Do not write raw FAT sectors while its filesystem is mounted. The app releases
mass storage before fastboot operations and reacquires the appropriate export
when later filesystem cleanup requires it.
