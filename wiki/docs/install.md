# Install Canoe

Use the hosted Canoe Boot Manager in a browser, or its WebUI in KernelSU. Both
runtimes share one deployment engine. The standalone commands are described in
[Command-line tools](./commands.md).

## Storage

The boot chain is:

```text
signed vulnerable ABL → raw efisp:BDS.efi → persist/efisp.fat → selected loader
```

`efisp` is a raw partition containing BDS. `efisp.fat` is a fully initialized
FAT16 file inside ext4 persist, sized from the available space in 8 MiB
increments between 8 and 256 MiB. Its mounted root contains `canoe.cfg`,
per-slot loader triplets, EFI tools and any manually installed BLS entries.
No GPT change is needed.

The old `/persist/efisp` directory is ignored and preserved. There is no import
or automatic migration. Users of gbl-chainload or Canoe 6.3.5 and earlier
must [reinstall and recreate entries](./reinstall.md).

## Hosted app

Open [canoe-boot-manager.1vv.ca](https://canoe-boot-manager.1vv.ca) in a
Chromium browser. WebUSB requires HTTPS or localhost, and the USB chooser needs
a click for each initially ungranted USB identity; remembered grants are reused
automatically when fastboot or CANOE-BDS managed storage returns. Linux needs
the one-time [scoped USB access rule](./linux-usb.md). Windows needs a fastboot
driver for its fastboot stage, while CANOE-BDS's managed interface requests the
built-in WinUSB driver automatically. Close other tools that own the same USB
interface. The app installs no OS ext4 driver and never mounts managed storage
as a disk.

From Overview, choose **Fresh install / redeploy**. This is the single entry for
a first Canoe deployment and for replacing any earlier EFISP modification. The
questions and observations that follow determine the images, cleanup, backup,
slot work and format-data assessment for this phone; do not try to select a
different workflow from an old scenario guide.

1. Answer the installation-history question **before** any
   initial ABL/BDS writes. A newly flashed efisp cannot establish whether the
   phone previously used another EFISP mod.
2. For a first installation, enter Android **Fastbootd**. Select a compatible
   vulnerable ABL and firmware ABL for recovery. Optionally select the previous
   BDS image for recovery; otherwise recovery of this initial step clears raw
   efisp. Review the active-slot and raw-efisp targets before applying.
3. Restart when ready. An unpopulated boot root normally enters Super Fastboot
   automatically. If it does not, open the BDS menu and choose Super Fastboot.
   The app verifies the initial writes there before continuing preparation.
4. Prepare the selected slot's loader and boot images, choose the intended mode,
   and review the data assessment and every write. Apply when satisfied.
5. Reboot only after completion. Formatting, when required, is a separate action
   in recovery; Canoe never formats automatically.

Initial writes and the later deployment share one saved operation. Closing the
app does not discard its receipts. Incomplete operations offer retry and,
where recovery inputs are available, reviewed Revert.

Fresh installation requires an independently saved persist backup before any
direct managed-USB ext4 edit. The hosted app offers a backup download or a
confirmation that you already hold one; Android saves through its own native
flow. Neither path flashes the whole persist partition.

Slot selection here is manual: A, B, or Both. Each selected slot has its own
preparation. Both-slot deployment writes the inactive slot before the active
slot. This is for manual reconciliation, not a system-OTA shortcut. An image
from another slot is not offered as an ordinary source; choose a file when
needed.

## KernelSU first setup and updates

Installing or updating the KernelSU module installs the manager and its bundled
tools only. It never deploys CANOE-BDS, changes boot partitions, imports a previous
modification, or formats phone data. There is no volume-key deployment dialog.

Follow KernelSU's normal reboot/activation instructions, then open the WebUI.
A WebUI visible before an update activates may still belong to the old module.
The activated WebUI retains full Deploy and native Android file pickers; provide
your firmware images again when needed. Other efisp modifications are not imported.

For a fully unlocked first deployment that requires formatting phone data, the
hosted manager is convenient because it remains available off-phone after the
format. Installing the KSU manager itself never introduces a format requirement.

For an installed system, use [OTA preparation](./ota.md), the Overview
boot-image shortcut, or [Uninstall Canoe](./uninstall.md). Installing or updating only the
manager does not require a data format.

## Image and data compatibility

A source ABL used to derive a loader is separate from the vulnerable ABL flashed
to the partition. Each `.efi` has matching `.gm2p` and `.tzmap` sidecars. Do not
mix generations. A parsed signing key does not establish OEM provenance or
firmware suitability.

See the [format-data matrix](./format-data.md). Missing boot evidence is
**Unknown**, not proof that formatting is required. AVB failures need correct
images and verification data; formatting does not repair them.
