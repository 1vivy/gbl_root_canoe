# Uninstall Guide


## 1. Backup Your Data

Before performing any uninstall operation, make sure to **fully back up all important data** to prevent data loss.


## 2. Hardware Re-lock Requirement

If the bootloader has been **hardware re-locked** (a real `fastboot flashing lock` — not a BDS boot mode), it **must be unlocked first** before proceeding. Use whichever unlock path your device supports: the vendor's own account-based unlock, or Superfastboot's `fastboot flashing unlock` while the chain is still installed. The reserve-token safeguard exists so that path survives a relock — but only if the chain was in place when the relock happened.


## 3. Uninstall Steps

1. Boot into **official fastboot** mode

2. Erase the patch partition:

   ```bash
   fastboot erase efisp
   ```

3. Format and wipe user data:

   ```bash
   fastboot -w
   ```
 
## Stray reference: no stock fastboot

> **Stray reference page:** this is not part of the main install flow.

If stock fastboot never appears and the only fastboot available is the
superfastboot served by the BDS, relock and wipe the chain in one session:

1. From the BDS, enter **superfastboot**.
2. Run:

   ```bash
   fastboot flashing lock
   fastboot erase efisp
   ```

Every lock-state hook in this project deliberately mutates the real
RPMB/DeviceInfo state to **unlocked** instead of merely swallowing the write;
that is what keeps red screens away. While the chainloaded ABL is running, the
device is therefore genuinely unlocked, so the relock must be performed from
inside superfastboot while it is still reachable.

Keep this order. `fastboot flashing lock` must complete while the chain is
still present; erasing `efisp` first stops the projection, and there may then
be no stock fastboot from which to relock. On devices that expose stock
fastboot, use the [ordinary uninstall path](./uninstall.md#3-uninstall-steps)
above instead.




## ⚠️ Important Notes

- 📌 Ensure the **bootloader is unlocked** according to your device's requirements before proceeding
- 📌 `fastboot -w` will **wipe the data partition** — confirm all important files are backed up beforehand
- 📌 After uninstallation, the device will be restored to its **unlocked, root state**
