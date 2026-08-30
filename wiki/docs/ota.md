# OTA update procedure

An OTA normally installs the next system generation into the other A/B slot.
The loader in that slot must be prepared before the device boots it.

## Required pre-reboot procedure

1. Start the system updater, install the OTA, and let it finish writing the
   other A/B slot.
2. Keep the device running in its current slot; do **not** reboot yet.
3. Open the KernelSU module WebUI and press **Install to inactive slot**.
4. The action requires known target-slot metadata. It derives and validates the
   target loader triplet, matching profile, and TrustZone map; unknown metadata
   is refused, and the running slot is never relabelled or used as a fallback.
   The prior valid generation for that target is preserved as
   `boot_backup.efi` with its sidecars.
5. Reboot after the action completes.

The action installs the target slot's `boot_a.efi` or `boot_b.efi` triplet and
its matching sidecars. A managed Mode 2 profile belongs to the installed
generation and is refreshed by this explicit action, never by the system
updater.

If the action is forgotten, the new slot carries a stock ABL. The GBL exploit is
absent, so BDS is simply not loaded and the device boots stock and unhooked.
Nothing is bricked. Boot back into the other slot, or run **Install to inactive
slot** and reboot again.

There is no OTA watcher in this release. The module performs this work only
when the operator presses the WebUI action before rebooting.

## Custom-ROM image and signer limits

The default derivation inputs are the device partitions. If the WebUI offers a
supplied ABL or vbmeta image, it must be an exact, non-empty file and is a
derivation input only, never a flash payload. The resulting pair must match the
firmware generation being installed.

A successful Mode 2 derivation means only that `vbmeta` parsed and carries a
signature and public-key blob. No tool here can prove which key is the OEM's.
The only automatic protection is detection of a changed public-key digest since
the last installed generation. A change is expected when moving to or from a
Custom ROM; follow the WebUI's explicit signer-change confirmation.

## Universal SCM safeguards

Modes 0, 1, and 2 best-effort suppress TrustZone fuse and anti-rollback SCM
requests during launch and refresh. This prevents further advancement only; it
cannot undo a blown fuse or lower an existing rollback floor. If the SCM
protocol is unavailable, launch continues and records `hooks-armed ... scm=0`.

## Xiaomi

Xiaomi fixed the GBL vulnerability in version **300**. As of version **306**,
XBL can still boot an older ABL to load `efisp` indirectly. Check the ABL
anti-rollback version before updating and test an OTA on a non-critical device.
A changed ABL that is incompatible with the device can still cause a hard
brick; the pre-reboot action does not make an incompatible vendor ABL safe.

Use a package freezer such as Hail when appropriate, and do not install an OTA
without confirming that the vulnerable ABL and the target firmware are
compatible.

## OnePlus

Newer OnePlus builds fix the loader path. Keep a vulnerable older ABL in the
partition and use **Install to inactive slot** after each OTA, before rebooting,
so the patched loader tracks the firmware generation. Builds through
`16.0.5.7xx` and below are vulnerable; newer builds may be fixed. Check the
device.

## Anti-rollback caution

If future firmware burns ABL anti-rollback versions, consider avoiding the OTA
or updating only HLOS. To identify HLOS images, extract `payload.bin` and look
for the `AVB0` header in each image before choosing partitions to flash.
