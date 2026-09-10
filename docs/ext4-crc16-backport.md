# Ext4 GDT checksum backport

The BDS ext4 driver remains the existing read-only driver. This change backports
the upstream CRC16-ANSI implementation and its corrected group-descriptor caller;
it does not refresh the complete driver or change its filesystem lifetime.

## Upstream sources

- [edk2 92288f433485e84863047fae698614c6785869d1](https://github.com/tianocore/edk2/commit/92288f433485e84863047fae698614c6785869d1),
  **MdePkg/BaseLib: Add CRC16-ANSI and CRC32c implementations**: the CRC16-ANSI
  table and helper are copied into the existing `BaseLib/CheckSum.c`. The CRC32C
  portion is intentionally not backported; Ext4Pkg retains its current helper.
- [edk2 e2d4f759137dea60a402a1810a4014a7cf00116b](https://github.com/tianocore/edk2/commit/e2d4f759137dea60a402a1810a4014a7cf00116b),
  **MdePkg/BaseLib: Fix CRC16-ANSI calculation**: retain raw accumulator semantics
  and `CRC16ANSI_INIT = 0xffff`, with no input/output complementation.
- [edk2-platforms b95395ba400e2ab45c902a80a965380538670f6b](https://github.com/tianocore/edk2-platforms/commit/b95395ba400e2ab45c902a80a965380538670f6b),
  **Ext4Pkg: Fix CRC16 checksumming on block groups**: use the BaseLib helper,
  start with `CRC16ANSI_INIT`, and omit the two-byte `bg_checksum` field from
  GDT_CSUM calculation. METADATA_CSUM continues including a zeroed checksum field.

The upstream BSD-2-Clause-Patent attribution is preserved. The obsolete private
`Ext4Dxe/Crc16.c`, its prototype and its INF source entry are removed; they have
no remaining callers. This backport only adds the CRC16 API to the older
Qualcomm BaseLib, which already compiles `CheckSum.c`.

## Regression mechanism

The inherited CRC16 table and complemented calculation were incorrect for ext4.
In 6.3.5, another inherited bug hid this: `Ext4HasGdtCsum()` tested the
METADATA_CSUM flag, so GDT_CSUM-only filesystems skipped descriptor verification.
Our subsequent correction of that predicate enabled the broken calculation.
The regression therefore resulted from our incomplete integration, despite the
checksum code itself being inherited.

A saved Android persist capture with `ro_compat = 0x7b` has a valid group-zero
checksum of `0xe72b`, confirmed independently by libext2fs. The old BDS routine
calculated `0xe469`; the backported production routine accepts the descriptor.
This is independent of `needs_recovery`: the original capture and a verified
filesystem-worker output with that flag cleared have the same valid checksum.

## Validation

`make -C submodules/uefi test` includes `test_ext4_checksums.py`. It requires
e2fsprogs (`mke2fs` and `libext2fs`) and creates disposable fixtures:

- 46/48 MiB partial groups, the 128 MiB Android-style geometry, and a 160 MiB
  two-group filesystem;
- 32- and 64-byte GDT_CSUM descriptors, plus METADATA_CSUM/CRC32C coverage;
- valid descriptors checked independently by libext2fs, then damaged data and
  checksum fields rejected by both libext2fs and production BDS admission;
- every descriptor byte changed individually in memory, including high fields,
  plus a different group number;
- unchanged read-only RECOVER admission for GDT_CSUM fixtures.

The harness links the actual `Superblock.c`, `BlockGroup.c`, private `Crc32c.c`
and BaseLib `CheckSum.c`. Disk reads use a host file; the root-inode open after
successful superblock/group admission is a stub. This proves checksum and
superblock admission, not a complete firmware mount or physical-device boot.
`CANOE_EXT4_CAPTURE=/absolute/path/persist.img` additionally checks a saved image
read-only. Captures and generated images are never committed.

The canonical `gbl_builder:latest` rebuild must produce a fresh `build/BDS.efi`
before testing on hardware. Existing small-partition handling and RECOVER
policy remain unchanged. The separate container-mapping METADATA_CSUM bitmap
helper concern is outside this targeted backport and still needs qualification.
