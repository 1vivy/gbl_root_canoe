# OTA preparation

After Android's updater finishes writing the inactive slot, remain in the
current system and open Canoe's **Install to inactive slot / OTA** action in
KernelSU **before rebooting**. This is an explicit operation, not an OTA watcher.

The flow defaults to the installed device mode and collapses its controls.
For custom boot images, the candidate defaults to active-slot boot material;
verification/donor material comes from the inactive target slot. Custom recovery
remains an explicit choice. Expand the image controls to choose files when
necessary, then prepare/check and review the exact resulting writes.

Only this Android OTA flow offers cross-slot sourcing. The hosted app's A/B/Both
selection is for manual reconciliation; each slot is prepared independently from
its own images or selected files. Slot selection itself creates no format-data
requirement.

Reboot after the operation completes and its readback is verified. If a step
fails, retain the saved operation and inspect Retry or Revert. Do not assume
that a stock or unserviced slot is bootable merely because an OTA wrote it.

A regular OTA can change vbmeta bytes while retaining its signing key. The
[data assessment](./format-data.md) compares effective identity and compatible
versions, including the installed Mode 2 profile. AVB/graft errors are separate
from storage-decryption compatibility; formatting does not repair an invalid
boot image.

## "Operation not permitted" when writing ABL

Android's updater leaves every partition it verified read-only until the next
reboot. Manager releases after 7.0.5 clear that flag on the reviewed partition
before writing, so the updated slot's `abl` can be written without rebooting.
On 7.0.5 and earlier, run `su -c 'blockdev --setrw /dev/block/by-name/abl_b'`
(the slot being written) and retry.

Kernels that include Baseband Guard (BBG), such as WildKernels, reject
writes to `abl` and `efisp` from root with the same error. When the kernel log
holds BBG's denial, the error message includes that line and the kernel
command-line values its allowlist uses. WildKernels' abl/efisp allowlist is
active only when the kernel command line contains
`oplusboot.secure_user_mode=0`. The AnyKernel3 zip adds it only if **Volume Up**
is pressed at its flash-time prompt; a timeout or Volume Down leaves the
allowlist off. Flashing the kernel image any other way leaves it off as well.
While `androidboot.slot_suffix` is on the command line, BBG allows only the
booted slot's `abl`, so the inactive slot's `abl` stays denied even with the
allowlist on. Check with:

```sh
su -c 'dmesg | grep baseband_guard; tr " " "\n" < /proc/cmdline | grep -E "secure_user_mode|slot_suffix"'
```

Writing the inactive slot's `abl` on such a kernel needs a BBG build that
allows it, or a write from the bootloader instead of Android.
