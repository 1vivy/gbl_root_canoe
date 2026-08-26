# Host-side install pathways

The host install uses the same two-bundle shape:

1. **Bootloader bundle (host only):** when needed, flash the vulnerable ABL,
   then flash `BDS.efi` to raw `efisp` with fastboot.
2. **Boot-root bundle:** install or refresh `persist/efisp`, derive the slot
   triplet, and UPSERT its `canoe.cfg` entry. This bundle runs through ADB on
   a device, or locally against a host-mounted BDS `oem mass-storage:persist`
   export.

Device-side callers use the same Bundle 2 scripts. When a device-side caller
cannot use Bundle 1 fastboot, its explicit raw BDS write remains in the
device installer.

The host toolkit invokes the same shared scripts as the device paths:
`tools/canoe-device/canoe_device_install.sh` owns the boot-root transaction,
and `tools/canoe-device/canoe_boot_entry.sh` is the only `canoe.cfg` writer.
The latter's UPSERT replaces only the named entry and preserves all others,
including hand-added custom-ROM entries and OTA-added slot entries. The BDS
itself only reads the resulting file.

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
| `canoe install [--via adb|mass-storage] [--boot-root PATH]` | install the boot root and upsert its boot entry |
| `canoe oneshot --abl IMG --mode 0\|1` | temp-root a locked device; writes nothing permanent |

`canoe build` on its own only *derives* artifacts. The BDS is written raw to
`efisp`, while `boot.efi` plus its sidecars and `canoe.cfg` live under the
persist partition's `efisp/` directory. The config format is specified in
`wiki/docs/canoe-cfg.md`.

## Bundle 1 — bootloader (host only)

The host operator completes this bundle with fastboot:

```bash
# only when the installed ABL lacks the GBL vulnerability
fastboot flash abl <vulnerable>.img
fastboot flash efisp BDS.efi
```

Omit the first command when the installed ABL already has the vulnerability.
The host never passes an `efisp` device to the shared transaction; `BDS.efi`
is the only raw whole-partition write in the host flow.

## Bundle 2 — boot root and boot entry

Bundle 2 installs or refreshes `persist/efisp`, derives the slot triplet, and
asks `canoe_boot_entry.sh` to UPSERT the corresponding `canoe.cfg` entry.

### ADB channel

Requires custom recovery with ADB enabled, or a rooted Android system. The
default route stages over ADB and runs the shared transaction on the device:

```bash
./canoe prep-device
./canoe install --via adb
```

`./canoe install` defaults to `--via adb`. To select an already-mounted host
boot root explicitly, use the local mount route:

```bash
./canoe install --boot-root <persist-mount>/efisp
```

The pull defaults to the active slot. Right after an `adb sideload`, pass
`--slot inactive` to derive from the other slot:

### BDS mass-storage channel

`./canoe install --via mass-storage` asks the running BDS to perform
`fastboot oem mass-storage:persist`, waits for the USB disk, mounts it
read-write, and runs the same transaction locally against the mounted
`persist/efisp`:

```bash
./canoe install --via mass-storage
```

The host waits for the USB disk, mounts it read-write, and runs the same
`canoe_device_install.sh` locally against the mounted `persist/efisp`. With an
already-mounted filesystem, including Windows WinFsp + LKL `lklfuse`, use:

```bash
./canoe install --boot-root <persist-mount>
```

After flushing and unmounting the host filesystem, press **Volume Down** on
the device to end the export session. Unplugging or a link loss does not end
the session; replugging resumes it.

## Firmware package preparation

For the Super Flasher / RegionalHybrid workflow, prepare the package without
reimplementing its flasher:

```bash
./canoe prep --pkg OOS_FILES_HERE \
             --recovery <custom>.img \
             --abl <vulnerable>.img \
             --in-place

bash Super_Flasher.sh
```

Complete Bundle 1 if needed, then boot custom recovery and run Bundle 2 over
ADB. On Windows, use `canoe.cmd` in place of `./canoe`. `--in-place`
substitutes the prepared images into the package and keeps
`<name>.img.canoe-orig` backups. The package's own slot and logical-partition
handling remains its responsibility.

`--abl` only changes which ABL image the flasher writes to `abl`; sidecars are
derived from the package's stock `abl.img` + `vbmeta.img` pair.

## How staging works

`canoe install` validates the local artifacts and stages them in the boot root.
With `--via adb` (the default), it pushes the staging directory over ADB and
runs `canoe_device_install.sh` on the device. With `--via mass-storage`, or
with `--boot-root PATH`, it runs that same script locally against the mounted
boot root. Staging beside the destination makes the commit a rename rather
than a copy.

The boot-root transaction — snapshot, commit and rollback — lives in
`canoe_device_install.sh`, while `canoe_boot_entry.sh` is the only
`canoe.cfg` writer. Both scripts are shared by the host toolkit, the KernelSU
module and the OTA watcher, so host and on-device paths cannot drift apart.
The optional raw `efisp` backup/write/byte-for-byte verification is used only
by device-side callers that cannot use Bundle 1 fastboot; the host never passes
an `efisp` device to this transaction.

Guarantees:

- The staged set is pushed and validated before anything live is touched, so a
  failed transfer changes nothing.
- Everything the commit overwrites is snapshotted first: the live triplet, the
  existing backup generation, `canoe.cfg` and `tools/`. A rollback therefore
  never leaves one generation's loader beside another's menu tree.
- The previous generation is demoted to `boot_backup.efi` plus matching
  sidecars, which is a managed path the BDS recognises and an entry generated
  in `canoe.cfg`, so it is selectable from the boot menu.
- The persist tree is complete and synced before a device-side BDS write, so an
  interrupted run never leaves a live BDS pointing at half-installed sidecars.
- A failed first install leaves no partial `boot.efi` behind.
- Device-side raw BDS writes are preceded by a full `efisp` backup and followed
  by byte-for-byte comparison; either failure restores the partition.
- On each successful install the transaction writes an informational
  `.canoe.gen` record beside the triplet. Its exact format is
  `CANOEG1|<bds-sha256>|<boot.efi-sha256>|<gm2p-sha256>|<tzmap-sha256>`.
  Tree-only host installs use `-` for the BDS field.
- Before replacing managed files, the transaction enumerates the boot root.
  Entries outside the managed triplet and backup generation, `tools/`,
  transaction markers/temporaries and `.canoe.gen` are moved into
  `.canoe.foreign/` and reported one per line. Existing entries there are
  never overwritten; a suffixed name is used instead. This move is
  transactional: a failed install puts the entries back.
- The entry writer's UPSERT changes only its named entry and preserves every
  other entry, including hand-added custom-ROM entries and OTA-added slots.
- The `abl` partition is touched only by Bundle 1's explicit host fastboot
  command, never by the shared install scripts.

## Files

| file | role |
|---|---|
| `canoe` | unified host entry point for install, device preparation and package preparation |
| `canoe install` | validate, stage and invoke the shared install transaction |
| `canoe prep-device` | derive the boot entry from the device |
| `canoe prep` | graft and substitute into a package |
| `canoe_device_install.sh` | shared boot-root transaction, run on-device over ADB or locally on a host mount |
| `canoe_boot_entry.sh` | sole `canoe.cfg` writer, shared by host, module and OTA watcher |
