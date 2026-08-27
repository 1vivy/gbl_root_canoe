# OTA update procedure

An OTA normally installs the next system generation into the other A/B slot.
The loader in that slot must be prepared before the device boots it.

## Required pre-reboot procedure

1. Install the OTA and let it finish writing the other slot.
2. Keep the device running in its current slot; do **not** reboot yet.
3. Open the KernelSU module WebUI and press **Flash To Other Slot**.
4. Wait for the action to derive and validate the target loader, profile, and
   TrustZone map. It labels the active `canoe.cfg` row with the slot that will
   boot next and preserves the prior generation as `boot_backup.efi`.
5. Reboot after the action completes.

The action derives from the target ABL when it is already vulnerable. Otherwise
it derives from the current ABL and copies that vulnerable ABL to the target
slot. The copy is a partition update; the generated `boot.efi` and its sidecars
are installed in the boot root. A managed Mode 2 profile belongs to the
installed generation and is refreshed by this action, never by an OTA.

If the action is forgotten, the new slot carries a stock ABL. The GBL exploit is
absent, so BDS is simply not loaded and the device boots stock and unhooked.
Nothing is bricked. Boot back into the other slot, or run **Flash To Other Slot**
and reboot again.

Automatic post-OTA patching is deliberately deferred. The module performs this
work only when the operator presses the WebUI action before rebooting.

## Custom-ROM image sources

The default derivation inputs are always the device partitions. The WebUI can
offer an independent toggle for each non-empty supplied file:

| Input | Supplied path | Default |
| --- | --- | --- |
| ABL | `/data/local/tmp/canoe/abl.img` | Target or current device partition as selected by the action |
| vbmeta | `/data/local/tmp/canoe/vbmeta.img` | Corresponding device partition |

A supplied image is a derivation input only and is never a flash payload. The
checkbox is disabled when its exact path is absent or empty. This permits a
Custom ROM to provide its own matching pair without changing the stock-ROM
path.

## Signer and Mode 2 limitation

A successful Mode 2 derivation means only that `vbmeta` parsed and carries a
signature and public-key blob. No tool here can prove which key is the OEM's.
The only automatic protection is detection of a changed public-key digest since
the last installed generation. A change is expected when moving to or from a
Custom ROM. An explicitly supplied `vbmeta` declares that choice; the module
allows that derivation and reports the signer change.

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
partition and use **Flash To Other Slot** after each OTA, before rebooting, so
the patched loader tracks the firmware generation. Builds through
`16.0.5.7xx` and below are vulnerable; newer builds may be fixed. Check the
anti-rollback version and wait for tested results before updating a primary
device.

## Anti-rollback caution

If future firmware burns ABL anti-rollback versions, consider avoiding the OTA
or updating only HLOS. To identify HLOS images, extract `payload.bin` and look
for the `AVB0` header in each image before choosing partitions to flash.
