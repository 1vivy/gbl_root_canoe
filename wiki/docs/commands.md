# Command-line tools

Canoe provides explicit commands that can be used without the GUI or WebView.
The old interactive `canoe` wrapper and Android `build.sh` workflow are retired.
The firmware release's rooted-Android one-shot archive includes the narrower,
non-interactive `install-canoe.sh` orchestration wrapper described below.

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

`install-canoe.sh` in
`canoe-one-shot-<CANOE_VERSION>-android-arm64.zip` performs a fresh installation
from a rooted Android shell. The archive is built by
`make target_one_shot_android` from the same firmware output attached to that
draft release; its identity is recorded in `manifest.json` and covered by
`SHA256SUMS`. Its baked assumptions are that `id -u` is `0`, persist is an
already-mounted ext4 directory, and SELinux is Permissive (or already Disabled)
for `--apply`. It accepts the active slot's stock ABL and
vbmeta as read-only firmware derivation sources, resolves only
`/dev/block/by-name/` partitions, and does not select another image. This
package does not unlock the bootloader or install an ABL.

Unpack the one-shot archive on the phone and restore the executable bits. The
archive stores them, but Android's `unzip` does not always apply Unix
permissions, and the resulting failure reads as a permission problem rather
than a packaging one:

```sh
cd /data/local/tmp
unzip -o canoe-one-shot-<CANOE_VERSION>-android-arm64.zip -d canoe
cd canoe
chmod 755 install-canoe.sh bin/*
```

Before applying, check `getenforce`. If it reports `Enforcing`, run
`setenforce 0` from the root shell and verify that `getenforce` now reports
`Permissive`. This is a **device-wide** reduction in SELinux enforcement,
not a permission limited to `efisp.fat`; root alone does not make the kernel
loop worker's backing-file I/O work under Enforcing. The one-shot archive
does not install the KernelSU policy; the installer neither checks nor changes
SELinux for you. Plan-only remains available under Enforcing. After `--apply`, use
`setenforce 1` to restore Enforcing (or reboot); further Android-side loop
access to `efisp.fat` under Enforcing requires an active policy grant.

Device mode is always explicit; there is no inherited or default mode.
`--persist-mount` is the already-mounted ext4 persist directory, commonly
`/mnt/vendor/persist` — confirm it with `mount | grep persist`, because the
wrapper neither mounts nor guesses it. `--work-dir` must be an unused scratch
directory:

```sh
./install-canoe.sh --mode 1 \
  --persist-mount /mnt/vendor/persist \
  --work-dir /data/local/tmp/canoe-work
```

It prints the plan and exits without touching storage:

```text
Active derivation slot: a
Derivation source, read only and never written: /dev/block/by-name/abl_a
Single raw write: partition=/dev/block/by-name/efisp image=/data/local/tmp/canoe/BDS.efi
Boot root: /mnt/vendor/persist/efisp.fat entry boot_a.efi mode 1
Boot prerequisite: an active ABL without the efisp redirect will not launch Canoe; install a compatible signed vulnerable ABL separately before expecting it to boot.
Work directory: /data/local/tmp/canoe-work (not a rollback backup)
Plan only: no storage was changed. Re-run with --apply to confirm this plan.
```

The work directory holds temporary prepared-loader staging and the raw-write
readback used for diagnostics. It is not a backup of persist or the previous
raw `efisp`, and it must not be treated as a rollback source. An independent
off-device persist backup and a separate recovery path are recommended before
`--apply`. The one-shot installer does not provide an old-`efisp` copy or
built-in rollback.

Review the plan's exact values, then repeat the same command with `--apply` to
confirm the destructive phase. Plan-only is the default. With `--apply`, the
wrapper creates the FAT container under persist and writes BDS to raw `efisp`;
it never writes an ABL partition. `canoe-provision create` refuses to proceed
if `persist/efisp.fat` already exists; the installer does not overwrite that
container.

The active slot's ABL and vbmeta are read-only derivation inputs. Stock ABL is
accepted: the wrapper uses it to prepare `boot_a.efi` or `boot_b.efi` and the
matching sidecars in the boot root. This completes preparation, not the
boot-chain prerequisite. Stock ABL has no `efisp` redirect, so it cannot launch
BDS from raw `efisp`; Canoe remains dormant until a compatible signed
vulnerable ABL is obtained independently and installed by the user or another
separately authorized tool.

Do not write the prepared `boot_<slot>.efi` to an ABL partition. Preparation
modifies it, so its signature no longer verifies and XBL would reject it before
it runs. It belongs in the managed boot root, where BDS launches it with the
security protocols bypassed for the duration of the load. Its `efisp` lookup is
also patched out by design.

During an applied run, the wrapper derives the loader triplet, creates
`persist/efisp.fat`, mounts that FAT container, installs the prepared
active-slot loader and EFI tools there, and creates an explicit `android-a` or
`android-b` default entry with the requested mode. The FAT container is cleanly
unmounted before the raw write; a loop device already auto-released at unmount
does not need a second detach. Only then is BDS written to raw `efisp` and
verified by readback into the work directory.
No prior-state `efisp` image is captured.

## Prepare and publish a loader

Use regular input image files and a new or empty staging directory:

```sh
canoe-image build --abl firmware-abl.img --vbmeta firmware-vbmeta.img --staged prepared
canoe-bootmgr --boot-root /mnt/canoe loader install --slot a --from prepared
canoe-bootmgr --boot-root /mnt/canoe entry set --id android-a --title 'Android A' \
  --image boot_a.efi --role active --mode 1 --default
```

Use the **root** `vbmeta.img` matching the firmware ABL for the prepared
loader. Do not substitute `vbmeta_system.img` or the AVB metadata embedded in
`boot.img` or `init_boot.img`. For Mode 2, the profile uses root-vbmeta
`boot.os_version` and `boot.security_patch` properties where present; each
falls back to its `init_boot.*` counterpart when absent. This supports root
vbmeta images such as myron's without mixing firmware generations or regions.

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
canoe-bootmgr --boot-root /mnt/canoe config set-policy --menu-mode menu \
  --menu-timeout-s 3 --fastbootd-mode2 true
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
