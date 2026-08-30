# Super Fastboot Usage Guide

## Entering Super Fastboot

- Temporarily boot BDS in RAM without writing flash:

  ```bash
  fastboot stage <BDS.efi>
  fastboot oem boot-efi
  ```

- When the OEM-unlocking warning appears during boot, press **Volume Up** to
  enter Super Fastboot.

### Both RAM-boot routes work, for different reasons

```bash
fastboot stage <BDS.efi>
fastboot oem boot-efi
```

```bash
fastboot boot <BDS.efi>
```

The second one surprises people because `BDS.efi` is a PE image, not an Android
boot image. It works because the *host* `fastboot` tool wraps whatever file it
is given into a synthetic boot image before sending it — the transcript says so:

```text
creating boot image...
creating boot image - 440320 bytes
Sending 'boot.img' (430 KB)                        OKAY
Booting                                            OKAY
```

438272 bytes of `BDS.efi` in, a 440320-byte boot image out. The bootloader's
boot handler then finds the `MZ` signature at the start of that image's kernel
section and launches the EFI payload rather than treating it as a kernel; that
is exactly what `IsEfiInBootImg` in `QcomModulePkg/Library/FastbootLib` is for.
Measured on the OnePlus 15.

Use whichever is convenient. `fastboot stage` + `fastboot oem boot-efi` is the
explicit form and does not depend on the host tool's wrapping behaviour;
`fastboot boot` is one command.

## First run and menu

`SfbBootRootIsEmpty` treats a missing or unreachable volume, a root with no
launchable image, and a config whose images are all absent as first run. BDS
shows a first-run screen with two rows:

- **Enter boot menu (Volume Up)**
- **Enter fastboot (default)**

The cursor starts on **Enter fastboot** and that screen waits two seconds.
Volume Up is the only key that opts into the normal menu; timeout, Volume Down,
and Power preserve the fastboot default. This is the safe path for a freshly
flashed BDS: the host can install the chain without any menu configuration.

For a populated root, BDS samples **Volume Up for one second at startup**,
before it brings up the filesystem stack, so the key is read before
initialization can consume it.

- **Volume Up held during that second** — the menu opens immediately and no
  unattended launch is attempted.
- **Volume Up not held** — BDS launches the entry named by `default` in
  `canoe.cfg` without showing the menu. The menu is reached if that launch
  returns or fails, or if the file names no `default`.

Once the normal menu is up, the configured `timeout` applies only to its first
draw when a valid config default exists. A key cancels that countdown; later
redraws wait indefinitely. `timeout 0` launches the default immediately rather
than waiting forever. A BLS row is not a valid `canoe.cfg default`, so BLS
tests must enter the menu deliberately.

The menu is built in this order:

1. **Session boot mode** (a next-launch override; it is never saved).
2. Existing `canoe.cfg` rows whose `image` exists.
3. If the config is absent or invalid, discovered boot-root compatibility rows
   for `boot.efi`, per-slot `boot_a.efi` and `boot_b.efi`, and
   `boot_backup.efi`. The current installer writes only the per-slot names and
   backup; `boot.efi` is a pre-b2 compatibility probe.
4. Per-volume `\EFI\BOOT\BOOTAA64.EFI` rows. `\EFI\DESC` supplies a label when
   present; otherwise the row is named `NONAME<n>`.
5. Usable BLS Type #1 rows from `\loader\entries\*.conf` on the persist ext4
   boot root or removable media. See
   [Chainloading and BLS entries](./chainload.md).
6. Built-in actions: **Enter Fastboot**, **Enter EFI Program Selector**,
   **EFI Tools**, **USB Mass Storage**, **Reboot to Recovery**, **Power Off**,
   and **Restart**.

Configured rows are shown only when their image exists; a missing image is
skipped. A BLS row is also skipped when its referenced image is missing or its
entry is invalid. Discovered rows are appended after configured rows and are
never an unattended default: `default` names only a `canoe.cfg` row, and the
default resolver selects only a non-removable plain EFI row. To boot a BLS
entry, hold Volume Up, navigate to it, and press Power. For unattended testing,
put a small wrapper UEFI application in a plain `canoe.cfg` row and make that
plain row the default; the wrapper can then select or chain to the BLS payload.

The menu includes the following session tools and actions in the same screen:

- **EFI Tools** lists files in the boot root's `tools/` directory.
- **USB Mass Storage** exports one partition as one USB disk. `persist` contains
  `/efisp`; `logfs` is offered only when it exists. BDS warns before exporting
  the live `persist` filesystem. See [`mass-storage.md`](./mass-storage.md).
- **Reboot to Recovery** resets directly to recovery. It is a built-in reset
  action, not a custom-image parser.

The shipped `SurfaceTools.efi` inventory opens from **EFI Tools**. Its default
views only enumerate UEFI protocol GUIDs, configuration-table GUIDs, loaded
image classes, memory descriptors, and known Qualcomm policy protocol presence;
they do not print raw addresses or call vendor methods. **Dump Passive Inventory
to logfs** explicitly overwrites `\SurfaceTools.log` on the already-mounted
`logfs` volume, flushes it, and closes every file handle before returning to
BDS. **Run Read-only Active Probes** requires a separate Volume Up confirmation
(Power cancels, so a held menu-select key cannot authorize the calls) before
calling exactly five documented getters for the maximum CPU index, TrustZone
version, verified-boot state, and Keymaster status. A successful call is
reported as `authorized`; the tool does not infer that an observed policy is
effective, and the active getters write no persistent state.

USB Mass Storage exports one partition as one USB disk. The same export is
available from fastboot:

```bash
fastboot oem mass-storage             # persist (default)
fastboot oem mass-storage:persist     # persist
fastboot oem mass-storage:logfs       # logfs
```

Only one partition is exported per session. **Volume Down on the device is the
only way to end a mass-storage session.** Disconnecting the cable does not end
it.

## Fastboot mode screen

While Super Fastboot waits for a host it shows its own rows, moved with Volume
Up/Down and chosen with Power:

- **Stay in Fastboot** - inert; it only repaints, and it is where the cursor
  starts so a stray keypress cannot do anything;
- **Reboot to Recovery**
- **Power Off**
- **Restart**

Recovery is here because the boot menu runs before the fastboot loop and cannot
be re-entered after **Enter Fastboot**. First-run also defaults directly to this
screen, so its **Reboot to Recovery** row is the recovery path after a host
install or export session.

From the host, the reboot target is honoured:

```bash
fastboot reboot              # Android
fastboot reboot recovery     # recovery
fastboot reboot bootloader   # back into Super Fastboot
```

Any other target fails rather than rebooting somewhere it was not asked to;
this device has no userspace fastbootd, so `fastboot reboot fastboot` is
refused instead of being answered with a bootloader reboot.

Ending the export itself is still Volume Down on the device. While the export
runs, the USB link is a mass-storage gadget and carries no fastboot channel, so
no host command can reach BDS. A host SCSI eject does end the session on this
hardware, but it is the vendor stack's side effect rather than a contract, and
canoe does not rely on it.

## Modes and DeviceInfo

The menu's mode selector is a session-only override for the next launch. It is
never saved. An entry's configured mode takes precedence, with file-global
`mode` as the fallback; see [`canoe-cfg.md`](./canoe-cfg.md).

- **Mode 0** is a hook-free passthrough and neither reads nor writes
  `DeviceInfo`.
- **Mode 1** projects the locked DeviceInfo view and applies the managed hooks.
- **Mode 2** additionally uses the matching 120-byte `.gm2p` profile for the
  managed `boot_a.efi`, `boot_b.efi`, or `boot_backup.efi` loader and the
  generated map. Its kernel cmdline blacklist handles
  `oplus_secure_guard_new` without repacking a boot image.

A Mode 1 or Mode 2 launch may repair `DeviceInfo` when its observed state does
not satisfy the requested policy. `devinfo-repair never` refuses that repair
and continues honestly in Mode 0; `asneeded` permits it. The boot log records
the observed state and action.

Mode 2's profile proves only that `vbmeta` parsed and carries a signature and
public-key blob. No tool here can prove which key is the OEM's. Automatic
protection detects a changed public-key digest relative to the installed
generation.

## Bootloader commands

Locking the bootloader triggers the platform's data-wipe behavior:

```bash
fastboot flashing lock
```

Unlocking without a data wipe uses:

```bash
fastboot flashing unlock
fastboot flashing unlock_critical
```

An inconsistent TEE state can cause the device to refuse the data key.

## Flashing and erasing

```bash
fastboot flash <partition> <file.img>
fastboot erase <partition>
```

The operator flashes a vulnerable ABL to `abl` and `BDS.efi` to `efisp` as
separate prerequisite operations; the host installer never writes a partition.

## Rebooting

```bash
fastboot reboot bootloader
fastboot reboot
```

This BDS implements the fastboot `reboot` handler for **Normal** mode only.
`fastboot reboot recovery` is not a recovery navigation command here. To enter
recovery, select **Reboot to Recovery** in the BDS menu or open the recovery
entry through **EFI Tools**.
