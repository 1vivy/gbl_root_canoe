# Command-line tools

Canoe provides explicit commands that can be used without the GUI, WebView,
Android slot discovery, or application recovery records. The old interactive
`canoe` wrapper and Android `build.sh` workflow are retired.

| Command | Responsibility |
| --- | --- |
| `canoe-bootmgr` | Configuration, entries, defaults, BLS and prepared loaders on an already mounted boot root |
| `canoe-image` | Inspect and prepare supplied images; resolve only the image helpers it needs |
| `canoe-provision` | Inspect, create or remove the container on explicitly supplied persist storage |
| `canoe-manager` | Application worker: OS adapters, deployment review, userdata assessment, snapshots, readback and recovery |

Each command validates its inputs and reports write/flush failures. The small
commands do not infer a device or run a deployment wizard. Snapshot, readback,
retry and Revert are application workflows. Use the GUI when those workflows
are wanted. `--help` lists each command's arguments; `--json` emits structured
results.

## Prepare and publish a loader

Use regular input image files and a new or empty staging directory:

```sh
canoe-image build --abl firmware-abl.img --vbmeta firmware-vbmeta.img --staged prepared
canoe-bootmgr --boot-root /mnt/canoe loader install --slot a --from prepared
canoe-bootmgr --boot-root /mnt/canoe entry set --id android-a --title 'Android A' \
  --image boot_a.efi --role active --mode 1 --default
```

`loader install` checks the triplet and publishes the sidecars before the EFI
file. Add `--replace` only when replacement is intended. It does not rotate an
old loader to a backup, assess data compatibility or write an ABL partition.
Image preparation uses private staging; helper failure preserves existing
caller files. Failed publication may leave an incomplete new staging directory;
choose a fresh directory before retrying.

## Entries and policy

```sh
canoe-bootmgr --boot-root /mnt/canoe config show
canoe-bootmgr --boot-root /mnt/canoe entry list
canoe-bootmgr --boot-root /mnt/canoe entry mode --id android-a --mode 2
canoe-bootmgr --boot-root /mnt/canoe config set-policy --menu-mode menu --menu-timeout-s 3
canoe-bootmgr --boot-root /mnt/canoe default set android-a
canoe-bootmgr --boot-root /mnt/canoe bls install --name pmos --entry pmos.conf \
  --artifact vmlinuz-canoe=Image --artifact initramfs-canoe=initramfs
```

Supply every image referenced by the BLS entry, including a DTB if used. BLS
publication places images before the entry. BLS removal removes the entry only.
Image paths inside the boot root use the firmware path grammar; input files
may use ordinary host paths, including spaces and Unicode.

Configuration changes preserve a validated previous configuration in
`canoe.cfg.prev`. Missing/malformed current configuration may use that fallback;
permission and I/O failures are reported, not treated as a missing file.

## Provision the container

On Linux or Android, with the actual ext4 persist filesystem already mounted:

```sh
canoe-provision inspect --persist-directory /mnt/vendor/persist
canoe-provision create --persist-directory /mnt/vendor/persist
```

Create refuses an existing `efisp.fat`. It does not mount the FAT file or import
`efisp/`. Mount the created file with the OS's vfat/loop support before passing
its mount point to `canoe-bootmgr`, and unmount normally when finished.

For an **offline ext4 image**, including on Windows:

```sh
canoe-provision inspect --persist-image persist.ext4 --ext4-helper /path/to/canoe-ext4
canoe-provision create --persist-image persist.ext4 --ext4-helper /path/to/canoe-ext4
```

The helper uses unchanged libext2fs. Routine FAT operations use the host's
native filesystem. Do not point an offline writer at a filesystem mounted by
another system. `remove` deletes only the unmounted container and preserves
unrelated persist contents, including the ignored legacy directory.
