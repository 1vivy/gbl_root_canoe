# Superfastboot Usage Guide

## Booting

- Temporarily boot the BDS in RAM (nothing is written to flash):

  ```bash
  fastboot stage <BDS.efi>
  fastboot oem boot-efi
  ```

- When OEM unlocking is enabled and the white warning text appears on boot, **press Volume Up to enter Superfastboot mode.**

## First run and boot menu

If the boot root contains neither `canoe.cfg` nor `boot.efi`, the BDS shows a first-run screen and goes straight to Super Fastboot. There is nothing to boot; fastboot is the only channel that can install anything.

The boot menu includes:

- **Reboot to Recovery**
- **USB Mass Storage**

USB Mass Storage exports one partition to a connected PC as a normal USB disk. `persist` contains the boot root at `/efisp` and is the repair channel for a device with no working ADB. `logfs` is offered only when that partition exists and is useful for pulling boot logs from a device that will not boot. Exporting `persist` shows a warning first because it is a live filesystem. Only one partition (one USB LUN) is exported per session; **Volume Down** ends the session.

The same feature is reachable from fastboot:

```bash
fastboot oem mass-storage             # persist (the default)
fastboot oem mass-storage:persist     # persist
fastboot oem mass-storage:logfs       # logfs
```

See the [USB Mass Storage guide](./mass-storage.md) for the full procedure and Windows mount step.

## Mode selection and lock state

The menu's mode row is a **session override**. It applies to the next launch and is never written anywhere. An entry with its own configured mode ignores the row because its `.gm2p`/`.tzmap` sidecars are bound to that exact policy. The persisted fallback is the file-global `mode` in [`canoe.cfg`](./canoe-cfg.md).

A Mode 1 or Mode 2 launch repairs the backing `DeviceInfo` only when the observed state does not already satisfy the requested mode. `lockstate never` in `canoe.cfg` refuses that repair; the launch then continues honestly in Mode 0. Mode 0 is a hook-free passthrough that neither reads nor writes `DeviceInfo`. The observed state is always recorded in the boot log.

## Bootloader (BL) Related

- Lock the BL **data wipe**:

  ```bash
  fastboot flashing lock
  ```

- Unlock the BL **without data wipe**:

  ```bash
  fastboot flashing unlock
  fastboot flashing unlock_critical
  ```

> Note: If the TEE status is inconsistent, the device will refuse to provide the data key, causing data access failure.

## Flashing

- Flash an image to a partition:

  ```bash
  fastboot flash <partition> <file.img>
  ```

- Erase a partition:

  ```bash
  fastboot erase <partition>
  ```

## Rebooting

- Reboot into bootloader; next normal boot enters official fastboot:

  ```bash
  fastboot reboot bootloader
  ```

- Reboot into recovery mode; next normal boot enters recovery:

  ```bash
  fastboot reboot recovery
  ```

- Normal reboot of device:

  ```bash
  fastboot reboot
  ```
