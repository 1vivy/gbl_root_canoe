# Install Canoe

Use Canoe Boot Manager on Linux or Windows, or its WebUI in KernelSU. The
application and the module installer use the same native deployment engine.
The standalone commands are described in [Command-line tools](./commands.md).

## Storage

The boot chain is:

```text
signed vulnerable ABL → raw efisp:BDS.efi → persist/efisp.fat → selected loader
```

`efisp` is a raw partition containing BDS. `efisp.fat` is a fully initialized
32 MiB FAT16 file inside ext4 persist. Provisioning requires a further 8 MiB of
free space. Its mounted root contains `canoe.cfg`, per-slot loader triplets,
EFI tools and any manually installed BLS entries. No GPT change is needed.

The old `/persist/efisp` directory is ignored and preserved. There is no import
or automatic migration. Users of gbl-chainload or Canoe 6.3.5 and earlier
must [reinstall and recreate entries](./reinstall.md).

## Desktop

Launch `canoe-boot-manager.sh` on Linux or `canoe-boot-manager.bat` on Windows.
Linux needs WebKitGTK 4.1 and its normal runtime dependencies; Windows needs
WebView2. Keep the packaged application and its helpers together. The GUI
requests privileges for protected device access and retains the authorized
helper for the session.

1. Start Deploy and answer the installation-history question **before** any
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

Desktop slot selection is manual: A, B, or Both. Each selected slot has its own
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

For an installed system, use [OTA preparation](./ota.md), the General boot-image
shortcut, or [Uninstall Canoe](./uninstall.md). Installing or updating only the
manager does not require a data format.

## Image and data compatibility

A source ABL used to derive a loader is separate from the vulnerable ABL flashed
to the partition. Each `.efi` has matching `.gm2p` and `.tzmap` sidecars. Do not
mix generations. A parsed signing key does not establish OEM provenance or
firmware suitability.

See the [format-data matrix](./format-data.md). Missing boot evidence is
**Unknown**, not proof that formatting is required. AVB failures need correct
images and verification data; formatting does not repair them.
