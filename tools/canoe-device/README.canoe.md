# canoe-device — device-side boot-root tools

This directory contains the POSIX `sh` implementation used by the on-device
installation paths. The host implementation is Python and does not invoke these
scripts.

| File | Purpose |
| --- | --- |
| `canoe_device_install.sh` | Validate, snapshot, commit, and roll back a boot-root generation |
| `canoe_boot_entry.sh` | Canonical `canoe.cfg` entry writer |

The KernelSU module uses these scripts for its first installation and its
post-OTA **Flash To Other Slot** action. The Android toolkit's temporary-root
wrapper also uses them in tree-only mode. Both scripts use POSIX `sh`; the
device does not need Python.

## Boot-root transaction

The transaction stages a complete set before touching live files:

```text
<stage>/boot.efi
<stage>/boot.efi.gm2p
<stage>/boot.efi.tzmap
<stage>/tools/                 optional
```

The loader must be non-empty, `.gm2p` must be exactly 120 bytes, and `.tzmap`
must be exactly 256 bytes. The transaction snapshots the current triplet,
backup triplet, `canoe.cfg`, and `tools/`, then commits the new generation. A
failure restores the snapshot. The previous generation becomes
`boot_backup.efi` with only its matching sidecars, and the managed config rows
are `android-a` or `android-b` plus `android-backup` when the backup loader is
non-empty. Hand-added rows remain unchanged.

`canoe.cfg` is written using the same grammar described in
[`wiki/docs/canoe-cfg.md`](../../wiki/docs/canoe-cfg.md). The BDS only reads the
resulting file. The `boot_a.efi` and `boot_b.efi` paths are passthrough paths,
not managed slot images; old files using those names are explicitly migrated
away.

## Entry writer

```text
sh canoe_boot_entry.sh set <boot-root> --id ID --title TITLE --image IMAGE \
  --role active|backup [--mode 0|1|2] [--default] \
  [--global-mode 0|1|2] [--timeout SECONDS] \
  [--devinfo-repair asneeded|never]
sh canoe_boot_entry.sh remove <boot-root> --id ID
sh canoe_boot_entry.sh show <boot-root>
```

`set` replaces only the named row and preserves other rows verbatim. The
installation transaction is the owner of generation rotation, sidecars, and
rollback; this writer is the owner of config serialization.

## Temporary-root tree installation

The Android toolkit wrapper requires root, validates the active slot, and
accepts only Mode 0 or Mode 1. It derives a matching triplet from the active
slot's ABL and `vbmeta`, or from optional supplied `--abl` and `--vbmeta` files.
It invokes `canoe_device_install.sh` with the boot root only; it does not write
a partition. The operator separately owns the `dd` of the vulnerable ABL and
`BDS.efi` to raw `efisp`.

## Recovery graft and vendor_boot

Mode 1 recovery preparation uses the standalone command:

```text
vbmetaport <official recovery vbmeta> <custom recovery.img> <output.img>
```

The output must not grow. The `vendor_boot` change is a fixed-offset cmdline
amendment that appends `module_blacklist=oplus_secure_guard_new` in place and
can be run repeatedly without changing an already patched image.
