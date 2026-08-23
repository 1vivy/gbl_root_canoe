# Host-side install pathways

Two independent pathways install the canoe boot chain. They share the staging
step and nothing else; pick one.

Both the Linux and the Windows toolkit ship the same set of scripts. Where this
document writes `canoe_prep_device.sh`, Windows users run `canoe_prep_device.bat`,
and so on. Options and behaviour are identical.

`build.sh` / `build.bat` on its own only *derives* artifacts. Nothing below
changes what the BDS or the sidecars are: the BDS is written raw to `efisp`, and
`boot.efi` plus `boot.efi.gm2p` / `boot.efi.tzmap` and `BOOTENTRIES` live under
the persist partition's `efisp/` directory.

## Pathway A — standalone

Requires only a **custom recovery with ADB enabled**. No firmware package, no
flasher, no vbmeta graft, and no root on the running system: persist is writable
from recovery.

```sh
# 1. in custom recovery, with adb up
./canoe_prep_device.sh        # pull abl + vbmeta, derive boot.efi/.gm2p/.tzmap
./canoe_stage.sh              # install the persist tree, then write the BDS

# 2. only if the abl partition is not already a GBL-vulnerable version
fastboot flash abl <vulnerable>.img
```

The pull defaults to the active slot. Right after an `adb sideload` — the usual
custom-ROM flow — the sideload wrote the *other* slot and has not booted it
yet; pass `--slot inactive` to derive from that slot instead.

Order matters. `canoe_prep_device.sh` derives `boot.efi` from `abl` and
`boot.efi.gm2p` from `vbmeta`, and those two must describe the **same** firmware.
Pulling both from the device gives a matching pair only while the `abl` partition
still holds its original image, so run it *before* flashing a downgraded ABL. If
you have already downgraded, supply a matching stock pair explicitly:

```sh
./canoe_prep_device.sh --abl stock_abl.img --vbmeta stock_vbmeta.img
```

The two flags must be given together — accepting one alone would reintroduce the
exact mismatch this guards against.

`boot.efi` and the `abl` partition do **not** need to be the same version. The
partition only has to carry the GBL vulnerability; the sidecars describe the
stock pair. `canoe_prep_device.sh` reports which case you are in, based on
whether `patch_abl` found the vulnerability in the source image.

## Pathway B — alongside a firmware package

For the Super Flasher / RegionalHybrid workflow, which ships both `.sh` and
`.bat`. This pathway does **not** reimplement the packaged flasher: it prepares
correct inputs, then the package's own script runs unmodified and never learns
canoe exists. Slot selection, `--slot=all` loops and logical-partition handling
all stay the flasher's business.

```sh
# 1. host side, no device needed
./canoe_prep.sh --pkg OOS_FILES_HERE \
                --recovery <custom>.img \
                --abl <vulnerable>.img \
                --in-place

# 2. run the package's own flasher, unchanged
bash Super_Flasher.sh          # or Super_Flasher.bat on Windows

# 3. boot the custom recovery, enable ADB
./canoe_stage.sh
```

`--in-place` substitutes the prepared images into the package directory and keeps
`<name>.img.canoe-orig` backups. Rerunning never overwrites an existing backup
with an already-substituted image.

Because the flasher writes the package's `recovery.img` to both slots, keeping a
custom recovery means the image it writes has to be the custom one — hence the
graft step. `canoe_prep.sh` lifts the official recovery vbmeta out of the
package's own `recovery.img` with `vbmetabackup -f` (host-side, no device) and
transplants it onto the custom recovery with `vbmetaport`, preserving the
partition size and the custom payload below `original_size`.

`--abl` only changes which ABL image the flasher writes to the `abl` partition.
Sidecars are always derived from the package's **stock** `abl.img` + `vbmeta.img`
pair, because that is the pair `boot.efi` and `boot.efi.gm2p` must agree with.

## How staging works

`canoe_stage.sh` and `canoe_stage.bat` are thin drivers. They validate the local
artifacts, push them into a staging directory inside the boot root, and hand the
actual transaction to `canoe_device_install.sh` running on the device. Staging
inside the boot root is deliberate: the staged files land on the same filesystem
as their destination, so the commit is a rename rather than a copy.

The transaction — snapshot, commit, rollback, the `efisp` backup and the
byte-for-byte verification — lives in that one device-side script and nowhere
else, so the two host drivers cannot drift apart.

Guarantees:

- The staged set is pushed and validated before anything live is touched, so a
  failed transfer changes nothing.
- Everything the commit overwrites is snapshotted first: the live triplet, the
  existing backup generation, `BOOTENTRIES` and `tools/`. A rollback therefore
  never leaves one generation's loader beside another's menu tree.
- The previous generation is demoted to `boot_backup.efi` plus matching sidecars,
  which is a managed path the BDS recognises and an entry the shipped
  `BOOTENTRIES` already lists, so it is selectable from the boot menu.
- The persist tree is complete and synced *before* the BDS is written, so an
  interrupted run never leaves a live BDS pointing at half-installed sidecars.
- A failed first install leaves no partial `boot.efi` behind.
- The BDS write is preceded by a full backup of `efisp` and followed by a
  byte-for-byte comparison of the written region; either failing restores the
  partition. The backup is pulled to the host either way.
- The preferred-mode record is left untouched unless `--mode N` is passed, in
  which case it is written after a successful install by an on-device
  `mode2_profile mode-write` (aligned read-modify-write, verified by reread)
  from the shipped `bin/mode2_profile-arm64`. It sits 3072 bytes before the end
  of `efisp`, outside the written region, and an absent or malformed record
  already means Mode 1. Modes can also be changed from the BDS menu or the
  module WebUI.
- The `abl` partition is never touched by any of these scripts.

## Files

| file | role |
|---|---|
| `build.sh` / `build.bat` | derive `boot.efi` + sidecars from `images/abl.img` + `images/vbmeta.img` |
| `canoe_lib.sh` | shared adb/slot/partition helpers for the Linux scripts (sourced) |
| `canoe_prep_device.sh` / `.bat` | pathway A preparation: derive from the device |
| `canoe_prep.sh` / `.bat` | pathway B preparation: graft + substitute into a package |
| `canoe_stage.sh` / `.bat` | host driver: validate, stage, invoke the device script; `--mode N` also sets the preferred boot mode |
| `canoe_device_install.sh` | the install transaction, executed on the device |
| `bin/mode2_profile-arm64` | Android-arm64 `mode2_profile`, pushed on-device by `canoe_stage --mode` |
