# Installation Guide

GBL Root Canoe keeps a signed, vulnerable ABL in `abl`, a raw `BDS.efi` in
`efisp`, and one or two current patched loader triplets in `persist/efisp`.
The BDS reads `canoe.cfg` from that boot root and chainloads the selected entry.
It never writes storage.

The boot root contains:

| File | Purpose |
| --- | --- |
| `canoe.cfg` | Boot policy, managed entries, and generation number |
| `boot_a.efi` + sidecars | Patched ABL for installed slot A |
| `boot_b.efi` + sidecars | Patched ABL for installed slot B |
| `boot_backup.efi` + sidecars | Previous generation of the last-updated slot, when present |
| `tools/` | EFI tools exposed by the BDS menu |

Each managed loader has a `.gm2p` profile (exactly 120 bytes) and a `.tzmap`
map (exactly 256 bytes). New installs do not write the retired `boot.efi`
name. A complete legacy `boot.efi` triplet is migrated to the explicit target
slot, while an incomplete legacy set is quarantined; see
[`canoe.cfg`](./canoe-cfg.md).

The `persist` filesystem is normally exposed at `/mnt/vendor/persist` by
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

The source `abl.img` and `vbmeta.img` used to derive a staged triplet must
describe one matching stock firmware pair. The vulnerable ABL left in the
partition may be older than that pair.

For a USB export, `canoe-ext4` (libext2fs) opens the raw ext4 source directly,
takes its exclusive lock, performs journal recovery before mutation, writes a
bounded transaction, and closes it cleanly. No host filesystem layer is used,
and the helper refuses a source that another writer already owns. The only
host permission required is permission to open the source device.


## The five supported scenarios

### 1. Host install

This is the first installation from a Linux or Windows computer. The device
must already be in Super Fastboot before the host surface runs:

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
then requests:

```text
fastboot oem mass-storage:persist
```

After `fastboot oem mass-storage:persist`, the host asks
`canoe-bootmgr source detect --json` for candidates and selects the first
readable, unmounted block row with identity `1209:ca0e` (or compatibility
identity `05c6:f000`). No drive letter or host filesystem directory is created.
`canoe-bootmgr` routes all boot-root reads and writes through `canoe-ext4`;
the helper creates `/efisp` when it is missing and the boot manager commits
the selected slot triplet, configuration, sidecars, and rollback as one
transaction:

```bash
canoe build --abl images/abl.img --vbmeta images/vbmeta.img
canoe install --slot a --mode 1
```

Use `--boot-root <persist>/efisp` only when a local directory is deliberately
provided for tests or an operator-managed workflow. For an image or raw block
source, use the boot manager's direct backend instead:

```bash
canoe-bootmgr --boot-root /path/to/efisp install \
  --staged /path/to/staged --slot a --mode 1
canoe-bootmgr --source /path/to/persist.ext4 install \
  --staged /path/to/staged --slot a --mode 1
canoe-bootmgr --ext4-image /path/to/persist.ext4 install \
  --staged /path/to/staged --slot a --mode 1
```

`--ext4-image` is an alias for `--source`; the two direct-source forms accept
an ext4 image or block device and cannot be combined with `--boot-root`.
`--slot a|b` is required for a direct install unless the caller deliberately
uses the inactive-slot form with known active metadata and
`--i-know-inactive-status`. An unknown slot is refused.

The bilingual `canoe-gui` is the graphical host surface over the same
`canoe-bootmgr` protocol:

```bash
canoe-gui --source /path/to/persist.ext4
canoe-gui --boot-root /path/to/efisp
canoe-gui --zh --source /path/to/persist.ext4
```

Its `--source`/`--ext4-image` and `--boot-root` choices are mutually exclusive.
It exposes slot status, config and BLS rows, and the install/post-OTA actions;
the GUI does not implement another config writer.

Super Fastboot publishes these fastboot variables:

| Variable | Value and meaning |
| --- | --- |
| `canoe-bds` | The project version. Its presence is the definitive signal that the device is running Super Fastboot. |
| `current-slot` | `a` or `b`. It is absent when the GPT marks neither slot or both slots. |

### 2. Host update

Run the same host command for a new matching firmware generation, selecting the
slot whose loader is being installed:

```bash
canoe build --abl images/abl.img --vbmeta images/vbmeta.img
canoe install --slot b --mode 1
```

The selected slot's existing triplet is copied to `boot_backup.efi` with
matching sidecars before the new triplet is committed. The
`android-backup` row exists while that previous generation is valid.
`android-a` and `android-b` rows are written only for slots with valid
installed triplets; hand-added rows remain verbatim. A managed install does
not invent a `default`; use `canoe-bootmgr default set` when one is wanted.

### 3. KernelSU module install

Install the module on a rooted device and follow its bilingual first-install
questionnaire. Select Mode 0, 1, or 2. Its device-side flow derives the
selected per-slot triplet, commits the boot root through `canoe-bootmgr`, and
performs any required device partition writes.

### 4. KernelSU update or post-OTA install

After the system updater finishes, stay in the current system and, **before
rebooting**, open the module WebUI and press **Install to inactive slot**. The
action requires target-slot metadata, derives and installs only the loader
triplet for the slot that will boot next, refreshes its matching sidecars, and
updates the managed row for that slot. It refuses unknown metadata and never
relabels or silently falls back to the running slot.

If the action is forgotten, the new slot retains its stock ABL. The GBL exploit
is absent there, so the BDS is not loaded and the device boots stock and
unhooked. Nothing is bricked: boot back to the other slot, or run
**Install to inactive slot** and reboot again.

A managed Mode 2 profile belongs to the installed generation. It is refreshed
only by the explicit action; the system updater does not refresh it. There is
no OTA watcher in this release.

If the WebUI offers supplied derivation images, they must be exact, non-empty
files matching the installed firmware generation; they are never flash payloads.

### 5. Locked-bootloader temporary root

For a temporary root on a locked device, use the Android toolkit's
`resources/build.sh`. It derives the staged triplet from the active slot and
then invokes the bundled `canoe-bootmgr` local-directory backend:

```sh
su -c sh ./build.sh --mode 0
su -c sh ./build.sh --mode 1
su -c sh ./build.sh --mode 1 --abl /path/abl.img --vbmeta /path/vbmeta.img
```

Only Mode 0 and Mode 1 are accepted by this wrapper. It changes only the boot
root tree, validates every generated file, removes the complete staged set on
failure, and performs no partition write. For a previously prepared staging
directory, the equivalent script-side command is:

```sh
canoe-bootmgr --boot-root /mnt/vendor/persist/efisp install \
  --staged /path/to/staged --slot a --mode 1
```

The `--boot-root` form is the local-directory backend; use `--source` or
`--ext4-image` for a direct ext4 image or block source instead. The old
`canoe_device_install.sh`, `canoe_boot_entry.sh`, and host `boottree.py` /
`bootsnap.py` writers are retired; `canoe-bootmgr` owns the transaction and
configuration rows. The operator owns the `dd` of the vulnerable ABL and
`BDS.efi` to `efisp`.

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
selected by export discovery:

```text
canoe-ext4.exe inspect \\.\PhysicalDrive<N>
```

No drive letter or third-party filesystem driver is used. If the package cannot
provide `canoe-ext4.exe`, packaging fails; there is no placeholder or silent
fallback. The native helper may be built with
`tools/canoe-ext4/build-windows.sh` on a host with MinGW and an e2fsprogs
source tree, then supplied to the package build.

## First run and Super Fastboot

### First-run behavior

An empty, absent, unreachable, or unusable boot root counts as first run. BDS
shows a first-run screen with **Enter boot menu (Volume Up)** and
**Enter fastboot (default)**. The cursor starts on fastboot and the screen
waits two seconds; timeout, Volume Down, and Power keep the fastboot default.
Volume Up explicitly opens the normal menu, where per-slot and other discovered
rows can be inspected before installation.

The BDS menu provides **USB Mass Storage** and **Reboot to Recovery** along with
the discovered/configured rows. USB Mass Storage exports one partition at a time;
`persist` is the partition containing `efisp`.

See [`usage.md`](./usage.md) for the menu and fastboot controls and
[`mass-storage.md`](./mass-storage.md) for the direct-source host procedure.

After a first Mode 1 installation, format data from the device menu:

```text
Main menu -> Reboot to Recovery -> FORMAT DATA
```

Mode 1 projects a locked DeviceInfo view to the OS. The TEE can refuse the data
key for userdata written under the previous state, so old data is unreadable
either way. `canoe.cfg` carries `devinfo-repair asneeded`; formatting makes the
new state coherent.

## Policy and source commands

Policy changes go through the single boot-root writer:

```bash
canoe config set-policy --menu-mode silent --key-window-ms 1200 \
  --menu-timeout-s 5
canoe default set android-a
canoe default set bls:pmos
canoe source detect --json
```

`default set bls:<stem>` refuses a stem that `bls list` cannot discover.
`source detect` is read-only and privilege-free for enumeration; it reports
`needs_privilege` when opening a source requires elevation.

## Double-click GUI entry point

On Linux, double-click the root-level `canoe-gui` launcher in the toolkit (or
run `./canoe-gui` from any current directory). It resolves the bundled
`bin/canoe-gui` and `bin/canoe-bootmgr`. On Windows, double-click the root-level
`canoe-gui.exe`; helper binaries remain under `bin/` and no console window opens.

The Connect screen displays detector candidates and provides one-click attach,
Refresh, and manual directory/image/device selection. It remembers the last
successful source in the platform config directory. Directory and image
sources do not need elevation. When a device operation is denied, Linux offers
**Retry with pkexec** and a copyable `sudo` command; Windows offers **Restart as
Administrator**. The GUI never escalates silently.
