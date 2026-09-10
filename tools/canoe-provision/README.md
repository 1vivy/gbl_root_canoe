# canoe-provision

Provision an 8–256 MiB FAT16 container at `efisp.fat` on supplied persist
storage. The primitive defaults to 8 MiB; the manager selects a size from measured available persist space after separately reviewed legacy cleanup and its reviewed headroom policy. Existing valid containers retain their size, including the earlier 32 MiB layout. The command never discovers a phone, mounts filesystems, changes GPT,
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
allocator-usable capacity, a 2 MiB free reserve, full byte initialization and the
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
deterministic empty templates in 8 MiB increments; the runtime copies the selected metadata prefix
and writes the complete zero-filled data area. The command contains no runtime
FAT formatter, FAT file writer, whole-volume transaction journal or compression
dependency. Installed systems maintain the contained filesystem through native
OS FAT support.

Portable `check`/`inspect` opens the image through the pinned `rust-fatfs`
filesystem library with writes prohibited. Existing FAT layouts do not need to
match the generated template: reserved sectors, root-directory size, FAT count
and dirty-state handling belong to the filesystem driver. Inspection does not
repair the image or clear its dirty flag. The enclosing container retains the
8–256 MiB size range and 8 MiB increments advertised by CANOE-BDS. Android's
worker opens its owned loop through the kernel vfat driver directly; it does not
run this portable probe before mounting or compare a second BPB interpretation
against the mounted filesystem.
