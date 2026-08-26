# Uninstall Guide

## 1. Back up your data

Before performing any uninstall operation, fully back up important data to prevent data loss.

## 2. Hardware re-lock requirement

If the bootloader has been **hardware re-locked** (a real `fastboot flashing lock`, not a BDS launch policy), it must be unlocked first. Use the unlock path supported by the device, such as the vendor's account-based flow or Superfastboot's `fastboot flashing unlock` while the chain is still installed. The reserve-token safeguard only helps when the chain was present when the re-lock happened.

## 3. Remove the boot root and BDS

1. Boot into **official fastboot** mode, or into a recovery that can access `persist`.
2. Remove `canoe.cfg` from the boot root if it is still present:
   - booted Android: `/mnt/vendor/persist/efisp/canoe.cfg`
   - recovery or an exported `persist`: `/persist/efisp/canoe.cfg`
3. Erase the raw BDS partition:

   ```bash
   fastboot erase efisp
   ```

4. Format and wipe user data if your device's uninstall procedure requires it:

   ```bash
   fastboot -w
   ```

Removing `canoe.cfg` clears the 7.x boot-root configuration; no separate loader policy remains to clear. If the boot root is not reachable before the raw partition is erased, the configuration becomes unused once `efisp` is gone, but remove it through Recovery or USB Mass Storage when possible.

## 4. If stock fastboot is unavailable

If the only fastboot available is the Superfastboot served by the BDS, complete the hardware re-lock and chain erase in one session:

1. From the BDS, enter Superfastboot.
2. Run:

   ```bash
   fastboot flashing lock
   fastboot erase efisp
   ```

Keep this order. The lock operation must complete while the chain is still present; erasing `efisp` first can remove the only route to the supported re-lock flow. If you can enter Recovery or USB Mass Storage first, remove `canoe.cfg` from `persist/efisp` as described above.

## Important notes

- Confirm the device-specific BL requirements before proceeding.
- `fastboot -w` wipes the data partition; verify that important files are backed up.
- After uninstall, the BDS chain is removed and the device returns to its normal unlocked/root state according to the device's remaining software.
