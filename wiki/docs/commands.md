# Command-line tools

Canoe provides explicit commands that can be used without the GUI or WebView.
The old interactive `canoe` wrapper and Android `build.sh` workflow are retired.
The Android toolkit now includes the narrower, non-interactive
`install-canoe.sh` orchestration wrapper described below.

| Command | Responsibility |
| --- | --- |
| `canoe-bootmgr` | Configuration, entries, defaults, BLS and prepared loaders on an already mounted boot root |
| `canoe-image` | Inspect and prepare supplied images; resolve only the image helpers it needs |
| `canoe-provision` | Inspect, create or remove the container on explicitly supplied persist storage |
| `canoe-manager` | Android native root worker for the KernelSU app: deployment review, userdata assessment, snapshots, readback and recovery |

Each command validates its inputs and reports write/flush failures. The small
commands do not infer a device or run a deployment wizard. Snapshot, readback,
retry and Revert are application workflows. Use the GUI when those workflows
are wanted. `--help` lists each command's arguments; `--json` emits structured
results.

## Rooted Android one-shot install

`install-canoe.sh` in the Android toolkit performs a fresh installation from a
rooted Android shell. Its baked assumptions are that `id -u` is `0` and that
the currently active slot's ABL and vbmeta are the correct firmware derivation
source. It reads `ro.boot.slot_suffix`, resolves only `/dev/block/by-name/`
partitions, and refuses an unsupported ABL rather than selecting another image.

Unpack the toolkit on the phone and restore the executable bits. The archive
stores them, but Android's `unzip` does not always apply Unix permissions, and
the resulting failure reads as a permission problem rather than a packaging
one:

```sh
cd /data/local/tmp && unzip -o toolkit_android.zip -d canoe && cd canoe
chmod 755 install-canoe.sh bin/*
```

Device mode is always explicit; there is no inherited or default mode.
`--persist-mount` is the already-mounted ext4 persist directory, commonly
`/mnt/vendor/persist` — confirm it with `mount | grep persist`, because the
wrapper neither mounts nor guesses it. `--backup-dir` must not already exist:

```sh
./install-canoe.sh --mode 1 \
  --persist-mount /mnt/vendor/persist \
  --backup-dir /data/local/tmp/canoe-rollback
```

It prints the plan and exits without touching storage:

```text
Active derivation slot: a
Derivation source, read only and never written: /dev/block/by-name/abl_a
Single raw write: partition=/dev/block/by-name/efisp image=./BDS.efi
Boot root: /mnt/vendor/persist/efisp.fat entry boot_a.efi mode 1
Rollback efisp: dd if='/data/local/tmp/canoe-rollback/efisp.before.img' of='/dev/block/by-name/efisp' bs=4194304
Plan only: no storage was changed. Re-run with --apply to confirm this plan.
```

Review those exact values, then repeat the same command with `--apply` to
confirm the destructive phase.

The active slot's ABL is a derivation source and is never written. The wrapper
refuses to continue unless `canoe-image abl-check` reports `gbl_patched` true
for it, because an ABL without the vulnerable loader cannot dispatch to `efisp`
at all.

Nothing here ever writes a patched ABL to a slot, and that is deliberate.
Patching edits bytes inside the image, so its signature no longer verifies.
XBL authenticates `abl` on the boot chain, so a patched image on the slot the
phone boots from is rejected before it runs: that is an EDL recovery, not a
failed boot. The patched image is instead the managed `boot_a.efi` or
`boot_b.efi` loader inside the boot root, which BDS launches with the security
protocols bypassed for the duration of the load, exactly because it is
unsigned. It also has its `efisp` lookup patched out, so it could not dispatch
to `efisp` even if it did run.

Before the raw write, the wrapper copies the active ABL and raw `efisp` into the
named rollback directory, hashes the source and copy, and refuses to continue
unless both match. It derives the loader triplet, creates and mounts
`persist/efisp.fat`, installs the slot loader and EFI tools, and creates an
explicit `android-a` or `android-b` default entry with the requested mode. The
FAT container is cleanly unmounted and its loop device detached before the raw
write begins. Raw `efisp` is the only partition written, and it is verified by
readback; a failure after it starts prints the exact `dd` rollback command. Keep
the rollback directory off the device after installation.

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

Offline ext4 work belongs to the hosted app, which drives persist over managed
USB with its own Rust ext4 driver; no separate libext2fs helper is packaged or
supported. Routine FAT operations use the host's native filesystem. Do not point
an offline writer at a filesystem another system has mounted. `remove` deletes
only the unmounted container and preserves unrelated persist contents, including
the ignored legacy directory.
