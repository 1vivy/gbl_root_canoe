# Using Canoe

General/Overview reports the connected device, installed state and saved
operations. Deploy contains preparation, review and apply. Boot entries manages
EFI/BLS entries and defaults. Settings contains policy, language and uninstall;
Diagnostics exposes observations and operation records.

**Prepare Mode 1 boot images** opens preparation directly and defaults to active
slot verification material. It preserves the installed mode and excludes
provisioning, loader rebuilding, BDS/tools updates and entry-mode writes. Select
manual slots independently when needed.

[Installation](./install.md), [OTA](./ota.md), [uninstall](./uninstall.md),
[USB storage](./mass-storage.md) and [CLI commands](./commands.md) describe the
respective operations. Revert is available for incomplete saved operations;
successful receipts remain inspectable.

## Entering Super Fastboot

Super Fastboot is the BDS's own fastboot session. With an existing boot setup,
press **VOL UP during boot** to enter Super Fastboot directly. **VOL DOWN** opens
the BDS boot menu. An empty installation uses the menu countdown described below.

From Super Fastboot, RAM-load the raw UEFI payload directly:

```sh
fastboot boot BDS.efi
```

The Super Fastboot tooling handles the packaging; no manually prepared Android
boot-image wrapper is needed. The current OEM handler does not implement
`oem boot-efi`.

RAM-loading leaves the installed ABL and raw efisp images in place. BDS can still
write its normal logs. Confirm the new BDS version on the device: the command
acknowledges before `LoadImage`/`StartImage`, so a successful host response alone
does not prove the new BDS ran. A failed launch may require a manual restart
because the old USB session has already stopped.

For the first test, confirm BDS starts, opens its menu and enters Super Fastboot,
then check device detection, variable reads and reconnect behavior. Container
provisioning and managed Android boot are separate later tests.

Super Fastboot waives ABL's critical-partition status, so flashing works from
this BDS session; partitions inside `super` remain the exception. Stock
userspace `fastbootd` is used only by Deploy's fresh-install Provision stage.
## First run and BDS menu

When the boot root is absent or contains no launchable image, BDS opens its normal
boot menu with a temporary **Entering Super Fastboot** entry highlighted and a
three-second countdown. The permanent **Enter Super Fastboot** action stays
available; both use the same action and neither saves a preference.
Power/Enter selects it; the volume keys cancel the countdown and navigate
normally. There is no separate first-run screen or additional key window.
Missing entries and an empty tools directory remain ordinary empty menus.
An unavailable filesystem is reported separately and enters Super Fastboot;
it is not classified as a new installation.

A populated root uses the configured key window (default **1200 ms**): **VOL UP**
enters Super Fastboot; **VOL DOWN** opens the boot menu. With no key, **Silent**
launches a resolvable saved default. **Menu** opens the menu with a default
three-second countdown when its saved default resolves to a boot entry. Beneath
the **Boot menu** title, a separate line shows **Highlighted entry will boot in
Xs.** Opening the menu with VOL DOWN or interacting with it cancels the countdown
and changes that line to **Timeout is disabled.** Returning from a submenu does
not restart it. Missing
defaults open the menu without a countdown.
Zero key-window or menu-timeout disables that respective wait.
An explicitly saved timeout keeps its configured value.

The menu uses a centered text block with no visible border, retaining equal inner
margins and a selection gutter. The bright **Boot menu** title and its separate
countdown line are centered, with a subdued build/version line below. Entry
details and actions align left; eight-dash separators divide their groups. The
selected row is highlighted across the full usable width. The subdued operating
instructions are centered beneath the actions. Long headings and instructions
wrap within the block; entry labels stay on one line and longer menus scroll.
The same renderer and navigation runner serve BDS submenus and the EFI file
browser. Standalone Android EFI tools keep their own interfaces.
Boot entries retain their existing discovery order. The grouped actions are:

- USB Mass Storage and Enter Super Fastboot.
- **Advanced**: Save a default entry, Change an Android entry's mode, Boot policy,
  Android EFI tools, and Select an EFI file.
- **Reboot**: Fastbootd, Bootloader, Recovery, and System.
- Power off and Restart.

Save default preserves each entry's mode. Mode changes preserve the default.
Boot policy edits the same `canoe.cfg` settings as CBM, including **Hide Booting…**.
Preferences are local until explicitly saved. A populated FAT container can hold
boot policy even when it has no entries; an absent container cannot save files.
**Restart** uses the ordinary boot path; **Reboot → System** also clears a known
recovery/Fastbootd BCB command. It preserves unrelated vendor BCB content.

Missing configured images and invalid BLS rows are skipped. To launch a BLS
row interactively, hold VOL DOWN during startup, select it, and press Power.
BLS rows are passthrough launches; Mode 1/2 policy hooks and managed sidecars do
not apply.

The **Advanced → Android EFI tools** action lists the boot-root `tools/` directory. The bundled
`SurfaceTools.efi` passive inventory and read-only policy probe behavior is
unchanged: the passive dump replaces only its explicitly named logfs file, and
the policy probe requires its separate VOL UP confirmation. See the BDS logs
for its bounded `key=value` report and remember that an `authorized` readback
does not prove physical debugging effectiveness.

## The Super Fastboot screen

While BDS waits for a host it shows:

- **Stay in Fastboot** — inert; it only repaints and is the initial cursor row;
- **Reboot to Recovery**;
- **Power Off**; and
- **Restart**.

From a host, the supported reboot targets are:

```bash
fastboot reboot              # Android
fastboot reboot recovery     # recovery
fastboot reboot bootloader   # bootloader target
fastboot reboot fastboot     # userspace Fastbootd
```

Other targets fail. Recovery and Fastbootd prepare the bootloader control block
in `misc`, flush it, and restart through the ordinary boot path. Firmware and
the selected boot chain must support that destination; this is not a stock-ABL
escape. System clears a known recovery/Fastbootd command before restarting.


## Saving a BDS preference

Normal boot/menu selections apply to that launch only. Use the explicit
**Save as default** action to persist a choice. It stages and flushes the
configuration, preserves a validated `canoe.cfg.prev`, then publishes the
current file. Missing or malformed current configuration can fall back to the
previous file; I/O errors remain errors.
