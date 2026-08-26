# Host-side install pathways

Two independent pathways install the canoe boot chain. They share the staging
step and nothing else; pick one.

Both the Linux and Windows toolkits ship one shared Python host implementation
behind one command. Linux runs `./canoe`; Windows runs `canoe.cmd`, which
invokes the bundled interpreter. Options and behaviour are identical.

Run it with no arguments for the interactive wizard, which asks what it needs
and is the intended path for a person. The subcommands below are the same work
without the questions, for scripts and CI:

| Command | What it does |
|---|---|
| `canoe` | interactive wizard |
| `canoe build` | patch the ABL and derive both sidecars |
| `canoe prep-device` | derive from the device's own `abl`/`vbmeta` |
| `canoe prep` | prepare alongside a firmware package |
| `canoe install` | install the boot root, then write the BDS |
| `canoe oneshot --abl IMG --mode 0\|1` | temp-root a locked device; writes nothing permanent |

`canoe build` on its own only *derives* artifacts. Nothing below changes what
the BDS or the sidecars are: the BDS is written raw to `efisp`, and `boot.efi`
plus `boot.efi.gm2p` / `boot.efi.tzmap`, `canoe.cfg` live under the persist
partition's `efisp/` directory. `canoe.cfg` is the declarative menu state the
BDS reads and never writes; its format is specified in
`wiki/docs/canoe-cfg.md`.

## Pathway A — standalone

Requires only a **custom recovery with ADB enabled**. No firmware package, no
flasher, no vbmeta graft, and no root on the running system: persist is writable
from recovery.

On Linux:

```bash
# 1. in custom recovery, with adb up
./canoe prep-device        # pull abl + vbmeta, derive boot.efi/.gm2p/.tzmap
./canoe install            # install the persist tree, then write the BDS

# 2. only if the abl partition is not already a GBL-vulnerable version
fastboot flash abl <vulnerable>.img
```

On Windows:

```bat
canoe.cmd prep-device
canoe.cmd install
```

The pull defaults to the active slot. Right after an `adb sideload` — the usual
custom-ROM flow — the sideload wrote the *other* slot and has not booted it
yet; pass `--slot inactive` to derive from that slot instead.

Order matters. `canoe prep-device` derives `boot.efi` from `abl` and
`boot.efi.gm2p` from `vbmeta`, and those two must describe the **same** firmware.
Pulling both from the device gives a matching pair only while the `abl` partition
still holds its original image, so run it *before* flashing a downgraded ABL. If
you have already downgraded, supply a matching stock pair explicitly.

On Linux:

```bash
./canoe prep-device --abl stock_abl.img --vbmeta stock_vbmeta.img
```

On Windows:

```bat
canoe.cmd prep-device --abl stock_abl.img --vbmeta stock_vbmeta.img
```

The two flags must be given together — accepting one alone would reintroduce the
exact mismatch this guards against.

`boot.efi` and the `abl` partition do **not** need to be the same version. The
partition only has to carry the GBL vulnerability; the sidecars describe the
stock pair. The preparation command reports which case you are in, based on
whether `patch_abl` found the vulnerability in the source image.

## Pathway B — alongside a firmware package

For the Super Flasher / RegionalHybrid workflow. This pathway does **not**
reimplement the packaged flasher: it prepares correct inputs, then the
package's own script runs unmodified and never learns canoe exists. Slot
selection, `--slot=all` loops and logical-partition handling all stay the
flasher's business.

On Linux:

```bash
# 1. host side, no device needed
./canoe prep --pkg OOS_FILES_HERE \
             --recovery <custom>.img \
             --abl <vulnerable>.img \
             --in-place

# 2. run the package's own flasher, unchanged
bash Super_Flasher.sh

# 3. boot the custom recovery, enable ADB
./canoe install
```

On Windows:

```bat
canoe.cmd prep --pkg OOS_FILES_HERE ^
               --recovery <custom>.img ^
               --abl <vulnerable>.img ^
               --in-place

canoe.cmd install
```

`--in-place` substitutes the prepared images into the package directory and keeps
`<name>.img.canoe-orig` backups. Rerunning never overwrites an existing backup
with an already-substituted image.

Because the flasher writes the package's `recovery.img` to both slots, keeping a
custom recovery means the image it writes has to be the custom one — hence the
graft step. `canoe prep` lifts the official recovery vbmeta out of the package's
own `recovery.img` with `vbmetabackup -f` (host-side, no device) and transplants
it onto the custom recovery with `vbmetaport`, preserving the partition size and
the custom payload below `original_size`.

`--abl` only changes which ABL image the flasher writes to the `abl` partition.
Sidecars are always derived from the package's **stock** `abl.img` + `vbmeta.img`
pair, because that is the pair `boot.efi` and `boot.efi.gm2p` must agree with.

## How staging works

`canoe install` is a thin driver. It validates the local artifacts, pushes them
into a staging directory inside the boot root, and hands the actual transaction
to `canoe_device_install.sh` running on the device. Staging inside the boot root
is deliberate: the staged files land on the same filesystem as their
destination, so the commit is a rename rather than a copy.

The transaction — snapshot, commit, rollback, the `efisp` backup and the
byte-for-byte verification — lives in that one device-side shell script and
nowhere else, so the host and the on-device module cannot drift apart.

Guarantees:

- The staged set is pushed and validated before anything live is touched, so a
  failed transfer changes nothing.
- Everything the commit overwrites is snapshotted first: the live triplet, the
  existing backup generation, `canoe.cfg` and `tools/`. A rollback therefore
  never leaves one generation's loader beside another's menu tree.
- The previous generation is demoted to `boot_backup.efi` plus matching sidecars,
  which is a managed path the BDS recognises and an entry generated in
  `canoe.cfg`, so it is selectable from the boot menu.
- The persist tree is complete and synced *before* the BDS is written, so an
  interrupted run never leaves a live BDS pointing at half-installed sidecars.
- A failed first install leaves no partial `boot.efi` behind.
- The BDS write is preceded by a full backup of `efisp` and followed by a
  byte-for-byte comparison of the written region; either failing restores the
  partition. The backup is pulled to the host either way.
- On each successful install the transaction writes an informational
  `.canoe.gen` record beside the triplet. Its exact format is
  `CANOEG1|<bds-sha256>|<boot.efi-sha256>|<gm2p-sha256>|<tzmap-sha256>`.
  The four digests describe the BDS image and installed files from that run;
  tree-only installs use `-` for the BDS field. The record is snapshotted and
  rolled back with the rest of the tree, and nothing refuses to install or boot
  because of its contents.
- Before replacing managed files, the transaction enumerates the boot root.
  Entries outside the managed triplet and backup generation, `tools/`,
  transaction markers/temporaries and `.canoe.gen` are moved into
  `.canoe.foreign/` and reported one per line. Existing entries there are never
  overwritten; a suffixed name is used instead. This move is transactional:
  a failed install puts the entries back.
- The transaction writes `canoe.cfg` atomically inside the same pair
  transaction as the live triplet and backup generation. It records the active,
  inactive (when a slot image is present), and backup roles, with mode stored
  per entry and a file-global fallback. A failed install restores the prior
  config byte-for-byte. The normative syntax and path rules live in
  `wiki/docs/canoe-cfg.md`.
- The `abl` partition is never touched by any of these scripts.

## Files

| file | role |
|---|---|
| `canoe` | unified host entry point for the install, device-preparation and package-preparation subcommands |
| `canoe install` | validate, stage and invoke the device-side install transaction |
| `canoe prep-device` | pathway A: derive the boot entry from the device |
| `canoe prep` | pathway B: graft and substitute into a package |
| `canoe_device_install.sh` | the install transaction, executed on the device |
