# GBL Root Canoe

[中文版](README_zh.md)

Canoe supplies a managed boot environment for supported Qualcomm devices. Its
hardware boot state and the state presented to Android are distinct; Mode 1/2
can present a locked device without relocking the physical bootloader.

```text
signed vulnerable ABL → raw efisp:BDS.efi → persist/efisp.fat → selected loader
```

The boot root is a 32 MiB FAT16 container on ext4 persist. BDS mounts it, loads
per-slot EFI/GM2P/TZ-map triplets and offers ordinary EFI/BLS entries. Explicit
Save as default persists a choice; normal menu selection remains one-shot.

The 7.0.0-b4 release surface uses Canoe Boot Manager, one desktop/WebUI application. Its native worker also
serves the KSU installer and owns deployment assessment, review, readback,
receipts, retry and recovery. Small standalone commands manage a supplied
mounted root (`canoe-bootmgr`), prepare images (`canoe-image`) and provision the
container (`canoe-provision`). Routine FAT access uses native OS filesystems;
unchanged libext2fs is confined to offline ext4 provisioning/removal.

- [Install on desktop or KernelSU](wiki/docs/install.md)
- [Reinstall from gbl-chainload or Canoe <=6.3.5](wiki/docs/reinstall.md)
- [Commands](wiki/docs/commands.md) and [configuration](wiki/docs/canoe-cfg.md)
- [OTA](wiki/docs/ota.md) and [format-data assessment](wiki/docs/format-data.md)
- [USB Mass Storage](wiki/docs/mass-storage.md) and [uninstall](wiki/docs/uninstall.md)
- [Build packages](wiki/docs/build.md)

Legacy ext4 `efisp/` directories are preserved and ignored. Recreate entries;
there is no legacy import. A matching signer does not prove OEM identity or
that a firmware image is suitable for the target slot.

## License and history


The project is GPL-2.0-or-later. [`ARCHIVE.md`](ARCHIVE.md) records historical
6.x material; the current release surface is the linked guides below.
