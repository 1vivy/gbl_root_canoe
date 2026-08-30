# Installation Guide

GBL Root Canoe keeps a signed, vulnerable ABL in `abl`, a raw `BDS.efi` in
`efisp`, and the current patched loader in `persist/efisp`. The BDS reads
`canoe.cfg` from that boot root and chainloads the selected entry. It never
writes storage.

The boot root contains:

| File | Purpose |
| --- | --- |
| `canoe.cfg` | Boot policy, managed entries, and generation number |
| `boot.efi` | Patched ABL for the installed generation |
| `boot.efi.gm2p` | 120-byte profile derived from matching `vbmeta` |
| `boot.efi.tzmap` | 256-byte map derived from the unpatched ABL |
| `boot_backup.efi` and sidecars | Previous generation, when an update exists |
| `tools/` | EFI tools exposed by the BDS menu |

The `persist` filesystem is normally mounted at `/mnt/vendor/persist` by
Android and at `/persist` by recovery. Its `efisp/` directory is the boot root.
Do not flash `persist`: it is a live filesystem that also holds vendor data.

## Prerequisites

The ABL in the active `abl` partition must contain the GBL vulnerability. If it
does not, the operator must first flash an older vulnerable stock ABL. The
operator also flashes `BDS.efi` to raw `efisp`:

```bash
fastboot flash abl <vulnerable>.img       # only when the current ABL is fixed
fastboot flash efisp BDS.efi
```

The `boot.efi`, `.gm2p`, and `.tzmap` files must describe one matching stock
firmware pair. The vulnerable ABL left in the partition may be older than that
pair.
The host never mounts the exported ext4 filesystem. `canoe-ext4` (libext2fs)
opens the exported block device directly, takes its exclusive lock, performs
journal recovery before mutation, writes bounded transactions, and closes it
cleanly. This path requires no root privilege beyond the operating system's
permission to open the USB device. The helper rejects a source that is mounted
by another writer.


## The five supported scenarios

### 1. Host install

This is the first installation from a Linux or Windows computer. The device
must already be in Super Fastboot before the interactive Python program runs:

```text
Linux:   ./canoe
Windows: canoe.cmd
```

`canoe` detects Super Fastboot by reading the `canoe-bds` fastboot variable. If
that variable is absent, it warns that `fastboot oem mass-storage:persist` does
not exist outside the BDS and asks for confirmation before continuing.

The interactive flow waits for `images/abl.img` and `images/vbmeta.img`, reads
the active slot from `current-slot` when the BDS publishes it, and asks which
slot is active only with an older BDS that does not publish that variable. It
then reaches the boot root through:

```text
fastboot oem mass-storage:persist
```

The host discovers the resulting Canoe USB disk (`1209:ca0e`; stock
`05c6:f000` is accepted for older firmware) and passes its raw block-device
path directly to `canoe-bootmgr`. No mount point or drive letter is created.
`canoe-bootmgr` routes all boot-root reads and writes through `canoe-ext4`.
The helper creates `/efisp` when it is missing, and the boot manager commits
the triplet, configuration, sidecars, and rollback as one transaction. To
script the same work, build the artifacts first and then install:

```bash
canoe build --abl images/abl.img --vbmeta images/vbmeta.img
canoe install --slot a --mode 1
```

Use `--boot-root <persist>/efisp` only for a local directory backend, tests, or
an operator-managed mount. The default export path is unmounted and direct.
The host program is Python and does not invoke a shell.

Super Fastboot publishes these fastboot variables:

| Variable | Value and meaning |
| --- | --- |
| `canoe-bds` | The project version. Its presence is the definitive signal that the device is running Super Fastboot. |
| `current-slot` | `a` or `b`. It is absent when the GPT marks neither slot or both slots. |

### 2. Host update

Run the same host command for a new matching firmware generation:

```bash
canoe build --abl images/abl.img --vbmeta images/vbmeta.img
canoe install --boot-root <persist-mount>/efisp --slot b --mode 1
```

The existing triplet is moved to `boot_backup.efi` with matching sidecars before
the new triplet is committed. The `android-backup` row is present while that
loader is non-empty. Hand-added rows remain verbatim; the active row is always
labelled with the slot supplied to `--slot`.

### 3. KernelSU module install

Install the module on a rooted device and follow its first-install questionnaire.
It selects Mode 0, 1, or 2. Mode 1 additionally asks for confirmation after
this required recovery step:

```text
vbmetaport <official recovery vbmeta> <custom recovery.img> <output.img>
```

The grafted output must not grow. The questionnaire then offers an in-place
`vendor_boot` cmdline patch. The module derives the triplet from the device
partitions by default, commits the boot root, and performs the raw partition
writes needed for the device-side installation. It prints a data-formatting
reminder after success.

Each image source can independently be changed to a non-empty supplied file:
`/data/local/tmp/canoe/abl.img` and
`/data/local/tmp/canoe/vbmeta.img`. The default is always the corresponding
device partition. A supplied image is a derivation input only; it is never a
flash payload.

### 4. KernelSU update or post-OTA install

After installing an OTA, stay in the current system and, **before rebooting**,
open the module WebUI and press **Flash To Other Slot**. The action derives the
loader for the slot that will boot next, installs it as the new `boot.efi`,
refreshes its profile and map, and copies the vulnerable ABL to the target slot
when required. The active row is labelled for that next slot.

If the action is forgotten, the new slot retains its stock ABL. The GBL exploit
is absent there, so the BDS is not loaded and the device boots stock and
unhooked. Nothing is bricked: boot back to the other slot, or run the action
and reboot again.

A managed Mode 2 profile belongs to the installed generation. It is refreshed
only by pressing **Flash To Other Slot**; an OTA never refreshes it. Automatic
post-OTA patching is not part of this release.

The WebUI offers the same independent supplied-image toggles described above.
They are enabled only when the exact files exist and are non-empty; otherwise
the device partitions remain the source.

### 5. Locked-bootloader temporary root

For a temporary root on a locked device, use the device-side shell wrapper in
the Android toolkit. It runs from the active slot and changes only the boot-root
tree:

```sh
su -c sh ./build.sh --mode 0
# or --mode 1
su -c sh ./build.sh --mode 1 --abl /path/abl.img --vbmeta /path/vbmeta.img
```

Only Mode 0 and Mode 1 are accepted. Mode 2 belongs to the module/WebUI path.
The wrapper defaults to the active slot's partition images; optional `--abl`
and `--vbmeta` values are derivation inputs. It validates every generated file,
removes the complete staged set on failure, and performs no partition write.
The operator owns the `dd` of the vulnerable ABL and `BDS.efi` to `efisp`.

## Matching images and signer changes

`images/abl.img` and `images/vbmeta.img` must be stock files for the firmware
being booted. A successful Mode 2 derivation means only that `vbmeta` parsed and
contains a signature and public-key blob. No tool here can prove which key is
the OEM's. The automatic protection is limited to detecting whether the public
key digest changed since the last installed generation.

A signer change is expected when moving to or from a custom ROM. The host asks
for `--allow-new-signer` before accepting it; the device module allows the
explicitly supplied `vbmeta` path and otherwise keeps the safe mode selection.

## Windows host tools

The Windows archive bundles `canoe-bootmgr.exe`, `canoe-ext4.exe`, and the
platform-tools fastboot executable. The helper operates on the raw source
selected by the export discovery:

```text
canoe-ext4.exe inspect \\.\PhysicalDrive<N>
```

No WinFsp driver or drive-letter mount is used. If the package cannot provide
`canoe-ext4.exe`, packaging fails; there is no placeholder or silent fallback.
The native helper may be built with `tools/canoe-ext4/build-windows.sh` on a
host with MinGW and an e2fsprogs source tree, then supplied to the package
build.

## First run and Super Fastboot

### 7. First-run behavior

An empty, absent, or unreachable boot root counts as first-run. The BDS displays
its first-run screen and enters Super Fastboot automatically. There is no entry
to launch yet. Earlier builds treated an unreachable boot root as populated, so
they did not auto-enter fastboot and stranded a freshly flashed device until the
operator navigated the menu by hand.
The BDS menu provides **Reboot to Recovery** and **USB Mass Storage**. The
latter exports one partition at a time; `persist` is the partition containing
`efisp`.

See [`usage.md`](./usage.md) for commands and
[`mass-storage.md`](./mass-storage.md) for the complete export procedure.

After a first Mode 1 installation, format data from the device menu:

```text
Main menu -> Reboot to Recovery -> FORMAT DATA
```

Mode 1 projects a locked DeviceInfo view to the OS. The TEE can refuse the data
key for userdata written under the previous state, so old data is unreadable
either way. `canoe.cfg` carries `devinfo-repair asneeded`; formatting makes the
new state coherent.
