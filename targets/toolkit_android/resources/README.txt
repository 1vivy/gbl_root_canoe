Canoe Android command tools

Use bin/canoe-image with explicit regular ABL/vbmeta image files to prepare a
loader. Use bin/canoe-provision to create efisp.fat on mounted ext4 persist,
then mount that FAT file using Android's vfat/loop support. Pass its mount point
to bin/canoe-bootmgr for prepared loaders, config, entries and BLS.

These commands do not discover your slot, flash ABL/BDS, assess userdata or run
an interactive deployment. The former build.sh wrapper is retired. For guided
review, readback and recovery, use Canoe Boot Manager (KernelSU module).

See wiki/docs/commands.md in the source repository. Run each command with
--help for its exact interface. Never mix raw writes with a mounted filesystem.
