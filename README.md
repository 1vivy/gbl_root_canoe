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

The 7.0.0-b5 release surface is under reconstruction: hosted CANOE BOOT MANAGER
uses WASM and managed USB; KernelSU retains a packaged WebUI and native Android
worker. Desktop executable bundles and filesystem sidecars are retired. The
b4-final tag preserves the preceding stack. Small mounted-root and image commands
remain available. See [the b5 reconstruction audit](docs/b5-reconstruction.md)
for the implemented firmware contracts and outstanding device qualification.

The inherited operator guides below describe established boot behavior; their
older desktop packaging commands are historical until explicitly updated for b5.

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
