# canoe-provision

Provision the fixed 32 MiB FAT16 container at `efisp.fat` on supplied persist
storage. The command never discovers a phone, mounts filesystems, changes GPT,
imports legacy entries, installs a loader, or flashes a partition.

```
canoe-provision template --output empty.fat
canoe-provision check --image empty.fat
canoe-provision create --persist-directory /mnt/vendor/persist
canoe-provision inspect --persist-image persist.img --ext4-helper ./canoe-ext4
canoe-provision create --persist-image persist.img --ext4-helper ./canoe-ext4
canoe-provision remove --persist-image persist.img --ext4-helper ./canoe-ext4
```

`--persist-directory` uses an existing Linux/Android ext4 mount. Creation checks
allocator-usable capacity, the 8 MiB reserve, full byte initialization and the
kernel FIEMAP result before publishing the final name. An existing container is
never replaced. Detach any loop device before inspecting or removing its backing
file; the application owns its mount leases. Other persist files, including
legacy `efisp` directories, remain untouched.

`--persist-image` is an offline regular ext4 image. It invokes unchanged
libext2fs through the supplied `canoe-ext4` helper. A failed create can leave its
reported `.canoe-boot-volume-*` staging file for deliberate recovery. The
application keeps its snapshots, partition readback and recovery records outside
the filesystem being modified. These are not responsibilities of this command.

The build host requires dosfstools (`mkfs.fat`). `build.rs` generates and checks
a deterministic empty template; the runtime copies its fixed metadata prefix
and writes the complete zero-filled data area. The command contains no runtime
FAT formatter, FAT file writer, whole-volume transaction journal or compression
dependency. Installed systems maintain the contained filesystem through native
OS FAT support.
