# canoe-image

Inspect or prepare image files without a device, boot root, deployment session,
or installation-history assessment. Inputs remain inputs; choose separate output
files. This command does not flash or select a slot.

```
canoe-image build --abl abl.img --vbmeta vbmeta.img --staged prepared
canoe-image build --abl abl.img --probe
canoe-image vbmeta --image vbmeta.img
canoe-image check --image boot.img --vbmeta vbmeta.img --partition boot
canoe-image extract --image boot.img --output boot.vbmeta
canoe-image graft --vbmeta boot.vbmeta --image custom-boot.img --output prepared-boot.img
canoe-image vendor-boot --image vendor_boot.img --output prepared-vendor_boot.img
```

Use `--json` for one result object. Build, ABL checking and vbmeta inspection use
existing image helpers, found beside this executable or on PATH. `--tools DIR`
selects an authoritative helper directory. Only helpers needed by the requested
operation are resolved. Graft and vendor_boot preparation need no helper.

The manager application links this same library and supplies its reviewed helper
resolver. The library knows nothing about desktop sessions, Android root, USB,
UAC or the application's helper containment policy. Application snapshots,
partition readback and recovery remain application responsibilities.
