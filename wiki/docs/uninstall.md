# Uninstall Canoe

Open **Settings → Uninstall Canoe** on desktop or in the KSU WebUI. Select a
stock firmware ABL suitable for the active slot. **Also restore inactive slot**
is optional and unchecked; it has a separate picker and an unchecked **Use the
same ABL image** choice. The app cannot establish the inactive slot's firmware
state or bootability from its signer or an old snapshot.

Review the selected targets and sources. Uninstall restores and verifies the
inactive ABL first when selected, then the active ABL. It then wipes and
verifies raw efisp and removes the unmounted `persist/efisp.fat` container.
Unrelated persist contents, including legacy `efisp/`, remain untouched.

An error stops the operation and retains its records outside the removed
container. Use the saved operation's validated Retry to continue. Uninstall
uses forward recovery rather than reversing a deployment plan.

Grafted images, vendor_boot patches, userdata and the KSU manager module remain
unchanged. Remove the manager module separately when finished. After verified
completion, **Reboot to recovery** is available if you want to format there;
rebooting does not itself format anything.

**If you intend to relock the device, make sure your phone is completely stock
before attempting it.** Uninstall does not make the remaining system stock.

Revert is a separate action for incomplete recorded installations. Successful
operations retain receipts but do not offer Revert as an uninstall shortcut.
