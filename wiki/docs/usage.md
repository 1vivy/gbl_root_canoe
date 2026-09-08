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

Super Fastboot is the BDS's own fastboot session. Press **VOL UP during boot**
to open the BDS menu, then choose **Enter Super Fastboot**.

The b3/b4 `boot` command can RAM-load an EFI payload carried in an Android
boot-image wrapper. Use a correctly prepared wrapper; do not assume an arbitrary
host fastboot binary wraps a raw `BDS.efi` for you. The current OEM handler does
not implement `oem boot-efi`, so the previously documented `stage`/`boot-efi`
sequence is not supported by these sources.

RAM-loading leaves the installed ABL and raw efisp images in place. BDS can still
write its normal logs. Confirm the new BDS version on the device: the command
acknowledges before `LoadImage`/`StartImage`, so a successful host response alone
does not prove the new BDS ran. A failed launch may require a manual restart
because the old USB session has already stopped.

A nested launch inherits firmware drivers from its parent. In particular, b4's
container mapper requires its own Ext4Dxe file interface; an ext4 volume still
bound to b3's driver can be refused. Test launch is useful for initial menu/USB
checks but does not replace clean cold-boot/container validation.

Super Fastboot waives ABL's critical-partition status, so flashing works from
this BDS session; partitions inside `super` remain the exception. Stock
userspace `fastbootd` is used only by Deploy's fresh-install Provision stage.
## First run and BDS menu

When the boot root is missing or unreachable, has no launchable image, or has a
configuration whose images are all absent, BDS treats it as first run. The
first-run screen has these rows:

- **Enter boot menu (Volume Up)**
- **Enter Super Fastboot (default)**

On the first-run screen, press **VOL UP** to enter the normal boot menu. The
cursor starts on **Enter Super Fastboot (default)**. The two-second timeout,
VOL DOWN, and Power preserve the safe Super Fastboot path; choosing the boot
menu row lets an operator inspect the available entries.

For a populated root, BDS reads `menu-mode` and samples keys for `key-window`
milliseconds:

- **Silent** (fresh-install default): VOL UP opens the menu and then waits
  indefinitely; VOL DOWN exits directly to Super Fastboot; with no key held,
  BDS launches the configured default after the key window.
- **Menu**: VOL DOWN during the key window exits directly to Super Fastboot.
  Otherwise the menu opens and counts down for `menu-timeout` seconds before
  launching the default; any key cancels the countdown and makes the menu wait
  indefinitely.

`key-window` is `0..=10000` milliseconds and defaults to `1200`; zero disables
sampling. `menu-timeout` is `0..=300` seconds and defaults to `5`; it is used
only in Menu mode, and zero means never auto-launch. An unresolved default,
including an undiscovered `bls:<stem>`, opens the menu with the existing notice
and waits instead of falling through to another row.

A default may name an Android row or a discovered non-removable BLS Type #1 row,
for example `default bls:pmos`. A USB-hosted BLS row cannot be an unattended
default. The writer emits `menu-mode`, `key-window`, and `menu-timeout`; the
legacy `timeout` line is accepted by BDS only as a pre-b2 compatibility alias
and is never written.

The menu is built in this order:

1. A boot-mode override for this launch, with a separate explicit save action.
2. Existing `canoe.cfg` rows whose `image` exists.
3. If configuration is absent or invalid, compatibility rows for `boot.efi`,
   `boot_a.efi`, `boot_b.efi`, and `boot_backup.efi` when present. New installs
   use per-slot names; `boot.efi` is only a pre-b2 probe.
4. Per-volume `\EFI\BOOT\BOOTAA64.EFI` rows, labelled from `\EFI\DESC` or
   `NONAME<n>`.
5. Valid BLS Type #1 rows from `\loader\entries\*.conf` on the persist boot
   root or removable media.
6. Built-in actions: **Enter Super Fastboot**, **Enter EFI Program Selector**,
   **EFI Tools**, **USB Mass Storage**, **Reboot to Recovery**, **Power Off**,
   and **Restart**.

Missing configured images and invalid BLS rows are skipped. To launch a BLS
row interactively, hold VOL UP during startup, select it, and press Power.
BLS rows are passthrough launches; Mode 1/2 policy hooks and managed sidecars do
not apply.

The **EFI Tools** action lists the boot-root `tools/` directory. The bundled
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
fastboot reboot bootloader   # back into Super Fastboot
```

Other targets fail. This BDS session is not a stock userspace session, so
`fastboot reboot fastboot` is refused here rather than being treated as a
bootloader reboot. Deploy's fresh-install Provision stage is the only workflow
in this guide that asks for stock userspace `fastbootd`.


## Saving a BDS preference

Normal boot/menu selections apply to that launch only. Use the explicit
**Save as default** action to persist a choice. It stages and flushes the
configuration, preserves a validated `canoe.cfg.prev`, then publishes the
current file. Missing or malformed current configuration can fall back to the
previous file; I/O errors remain errors.
