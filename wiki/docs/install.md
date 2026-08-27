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

## The five supported scenarios

### 1. Host install

This is the first installation from a Linux or Windows computer. The operator
completes the two fastboot flashes above, boots the BDS into Super Fastboot,
and runs the interactive Python program:

```text
Linux:   ./canoe
Windows: canoe.cmd
```

The interactive flow waits for `images/abl.img` and `images/vbmeta.img`, asks
for the active slot and mode, and then reaches the boot root through only:

```text
fastboot oem mass-storage:persist
```

It mounts the exported `persist` filesystem, derives the triplet, commits the
boot root transaction, and writes the active row in `canoe.cfg`. A mode-1
installation also asks about recovery grafting and the optional `vendor_boot`
patch. To script the same work, build the artifacts first and then install them:

```bash
canoe build --abl images/abl.img --vbmeta images/vbmeta.img
canoe install --boot-root <persist-mount>/efisp --slot a --mode 1
```

`--boot-root` is optional when the computer should perform the BDS mass-storage
export itself. The host program is Python and does not invoke a shell.

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

The Windows archive bundles pinned platform-tools `fastboot.exe`, Ext4Windows,
and the WinFsp installer. Ext4Windows is invoked as:

```text
ext4windows.exe mount \\.\PhysicalDrive<N> Z: --rw
```

Its default mount is read-only; `--rw` is required for an installation. If the
bundle cannot attach the exported disk, run `ext4windows.exe --scan`, mount the
volume manually, and rerun:

```text
canoe.cmd install --boot-root <drive>:\efisp --slot a --mode 1
```

## First run and Super Fastboot

### 7. First-run behavior

If the boot root has neither `canoe.cfg` nor `boot.efi`, the BDS displays its
first-run screen and enters Super Fastboot. There is no entry to launch yet.
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
