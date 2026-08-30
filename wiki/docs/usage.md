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

If the boot root has neither `canoe.cfg` nor `boot.efi`, BDS shows first-run
information and enters Super Fastboot. There is no entry to launch yet.

### Reaching the menu instead of the default

BDS samples **Volume Up for one second at startup**, before it brings up the
filesystem stack, so the key is read before initialization can consume it.

- **Volume Up held during that second** — the menu opens immediately and no
  unattended launch is attempted.
- **Volume Up not held** — BDS launches the entry named by `default` in
  `canoe.cfg` without showing the menu. The menu is reached only if that launch
  returns or fails, or if the file names no `default`.

Once the menu is up, the countdown applies to the **first draw only**: it waits
`timeout` seconds and then launches the highlighted default. Any key press stops
the countdown, and every later draw waits indefinitely. `timeout 0` does not
mean "wait forever" — it launches the default immediately, so a config being
used to test a new row wants a generous `timeout`, not a zero one.

This matters when adding a hand-written row: a row that never gets selected
because the default launched first looks exactly like a row that failed to
enumerate.

The menu includes:

- **Reboot to Recovery**
- **USB Mass Storage**
- configured `Android (slot A)`, `Android (slot B)`, and `Android (previous)`
  rows when their managed files exist;
- every other `canoe.cfg` row whose `image` exists, including hand-written ones
  such as a third-party firmware chainload — see
  [Chainloading a third-party UEFI firmware](./chainload.md);
- **EFI Tools** for files in the boot root's `tools/` directory.

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

USB Mass Storage exports one partition as one USB disk. `persist` contains the
boot root at `/efisp`; `logfs` is offered only when it exists. BDS warns before
exporting the live `persist` filesystem. See [`mass-storage.md`](./mass-storage.md)
for the host procedure.

The same export is available from fastboot:

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

Recovery is here because the boot menu's row is out of reach from fastboot: the
menu runs before the fastboot loop starts, and on a device whose boot root is
empty it never runs at all. That row is what to reach for after an export
session, when the install is done and the device should go to recovery.

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
- **Mode 2** additionally uses the matching 120-byte `boot.efi.gm2p` profile and
  the generated map. Its kernel cmdline blacklist handles
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
