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

`android::inspect(bytes, Kind)` validates boot/recovery headers 0–4 and
vendor_boot headers 3–4, section ranges, page geometry and the selected image
role. `InitBoot` requires the Android 13+ v4 ramdisk-only layout. Dedicated
recovery may be ramdisk-only. Unsigned images are allowed by this structural
check; a v4 boot signature is range-checked but never reported as authenticated.
When an AVB footer exists, Android sections must fit before its original-payload
boundary. `partition::materialize(..., Kind::Android)` always runs this guard
before padding or relocating the footer.

The layout follows the [AOSP boot image definitions](https://android.googlesource.com/platform/system/tools/mkbootimg/+/refs/heads/main/include/bootimg/bootimg.h)
and [generic boot partition roles](https://source.android.com/docs/core/architecture/partitions/generic-boot).
This is separate from AVB authentication, graft-key matching, firmware suitability
and phone-data compatibility. Synthetic format tests cover every supported header
version; the b5 qualification also inspected the retained OP15 A/B boot, recovery
and vendor_boot images without writing a device.
