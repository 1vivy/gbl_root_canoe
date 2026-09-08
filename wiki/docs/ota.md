# OTA preparation

After Android's updater finishes writing the inactive slot, remain in the
current system and open Canoe's **Install to inactive slot / OTA** action in
KernelSU **before rebooting**. This is an explicit operation, not an OTA watcher.

The flow defaults to the installed device mode and collapses its controls.
For custom boot images, the candidate defaults to active-slot boot material;
verification/donor material comes from the inactive target slot. Custom recovery
remains an explicit choice. Expand the image controls to choose files when
necessary, then prepare/check and review the exact resulting writes.

Only this Android OTA flow offers cross-slot sourcing. Desktop A/B/Both is for
manual reconciliation; each slot is prepared independently from its own images
or selected files. Slot selection itself creates no format-data requirement.

Reboot after the operation completes and its readback is verified. If a step
fails, retain the saved operation and inspect Retry or Revert. Do not assume
that a stock or unserviced slot is bootable merely because an OTA wrote it.

A regular OTA can change vbmeta bytes while retaining its signing key. The
[data assessment](./format-data.md) compares effective identity and compatible
versions, including the installed Mode 2 profile. AVB/graft errors are separate
from storage-decryption compatibility; formatting does not repair an invalid
boot image.
