# GBL Root Canoe

[中文版](README_zh.md)

CANOE-BDS supplies a managed boot environment for supported Qualcomm devices.
Mode 1/2 can present a locked device to Android without relocking the physical
bootloader.

```text
signed vulnerable ABL → raw efisp:BDS.efi → persist/efisp.fat → selected loader
```

The boot root is a FAT16 container on ext4 persist, sized from available space in
8 MiB increments (8–256 MiB). BDS mounts it, loads per-slot EFI/GM2P/TZ-map
triplets and discovers EFI/BLS entries. Ordinary selections affect this boot;
Advanced saves the default, managed-entry mode or boot policy independently.

The 7.0.0-b6 release surface is the hosted Canoe Boot Manager and activated
KernelSU WebUI. The [hosted app](https://canoe-boot-manager.1vv.ca) uses WASM and
managed USB; the module bundles its WebUI and native Android worker. Module
installation installs the manager only. Desktop executables and filesystem
sidecars are retired; the `7.0.0-b4-final` tag preserves the preceding stack.

- [Install from a host or KernelSU](wiki/docs/install.md)
- [Reinstall from gbl-chainload or CANOE-BDS ≤6.3.5](wiki/docs/reinstall.md)
- [Commands](wiki/docs/commands.md) and [configuration](wiki/docs/canoe-cfg.md)
- [OTA](wiki/docs/ota.md) and [format-data assessment](wiki/docs/format-data.md)
- [USB mass storage](wiki/docs/mass-storage.md) and [uninstall](wiki/docs/uninstall.md)
- [Release pipeline](wiki/docs/release.md)

Legacy ext4 `efisp/` directories are ignored and never imported. Recreate entries;
the app offers reviewed cleanup of old mod files. A matching signer does not
prove OEM identity or that a firmware image suits another slot. Beta release
notes state the qualification performed on each published build.

## License and history

The project is GPL-2.0-or-later. [ARCHIVE.md](ARCHIVE.md) records historical 6.x
material; [the b5 reconstruction audit](docs/b5-reconstruction.md) describes the
architecture change that preceded this beta.
