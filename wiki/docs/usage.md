# Superfastboot Usage Guide


## Booting

- Temporarily boot the BDS in RAM (nothing is written to flash):

  ```bash
  fastboot stage <BDS.efi>
  fastboot oem boot-efi
  ```


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


## Notes

- ⚠️ **Validation status:** Host builds and test fixtures are validated. On-device validation is scoped to a OnePlus Ace 6T (CPH2767, `macan`) running OxygenOS `CPH2767_16.0.9.401(EX01)`, using the RAM-only staged-BDS path; sidecar loading, hook arming, KeyMaster rewrites, and SCM drops were confirmed. No other device and no permanent installation have been validated.
- When OEM unlocking is enabled and the white warning text appears on boot, **press Volume Up to enter Superfastboot mode.**
