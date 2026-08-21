# Uninstall Guide


## 1. Backup Your Data

Before performing any uninstall operation, make sure to **fully back up all important data** to prevent data loss.


## 2. Hardware Re-lock Requirement

If the bootloader has been **hardware re-locked** (a real `fastboot flashing lock` — not a BDS boot mode), it **must be unlocked first** before proceeding:

| Device | Unlock Method |
|--------|---------------|
| **Twoplus** | Deep Test Unlock |
| **Dami** | Super Fastboot Unlock |


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


## ⚠️ Important Notes

- 📌 Ensure the **bootloader is unlocked** according to your device's requirements before proceeding
- 📌 `fastboot -w` will **wipe the data partition** — confirm all important files are backed up beforehand
- 📌 After uninstallation, the device will be restored to its **unlocked, root state**
