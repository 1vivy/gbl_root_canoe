# OTA update procedure

An OTA normally installs the next system generation into the other A/B slot.
That slot's Canoe loader must be prepared before the device boots it. The
post-OTA action is an explicit app/CLI operation; there is no background OTA
watcher.

## Required pre-reboot procedure

1. Start Android's system updater, install the OTA, and wait for it to finish
   writing the other A/B slot.
2. Keep the device running in its current slot. **Do not reboot yet.**
3. Open Canoe Boot Manager in the KernelSU surface. Overview reads the local
   boot root directly; it does not wait for fastboot.
4. From Overview open Deploy, choose the **refresh** lane, and keep the
   inactive slot as the explicit target. The stages **Provision → Prepare →
   Action** obtain slot metadata, check the transition and evidence, and apply
   the prepared target with `ota-apply` only after the target slot is known.
   The app never relabels the running slot or silently falls back to it. A
   missing or unknown slot is a refusal, not a guess.
5. Reboot only after the app reports success.

The equivalent writer command is explicit about the target:

```bash
canoe-bootmgr ota-apply --staged /path/to/staged \
  --target-slot b
```

Omitting `--mode` inherits the persisted mode. For an explicit mode change,
pass `--id <ENTRY_ID>` for an existing managed row, or `--from-mode 0|1|2`
for a new row, and repeat `--acknowledge <CODE>` for every acknowledgement
required by `mode.plan`. When image evidence is needed, add
`--current-vbmeta <PATH>`, `--target-vbmeta <PATH>`, and
`--target-image <PATH>`; those paths are evidence inputs, not implicit flash
payloads. The writer evaluates `mode.plan` before mutating the boot root; a
refusal or missing acknowledgement leaves it untouched. For example:

```bash
canoe-bootmgr ota-apply --staged /path/to/staged \
  --target-slot b --mode 1 --id android-b \
  --current-vbmeta <CURRENT_VBMETA> --target-vbmeta <TARGET_VBMETA> \
  --target-image <TARGET_IMAGE> \
  --acknowledge <CODE_1> --acknowledge <CODE_2>
```

Add the global `--boot-root`, `--source`, or `--ext4-image` selector when the
staged operation is against a particular local boot-root source. Use
`--allow-new-signer` only after reviewing and explicitly accepting the signer
change reported by the plan. The `ota-apply` operation installs the target
slot's `boot_a.efi` or `boot_b.efi` triplet and matching sidecars. The prior
valid generation is retained as `boot_backup.efi` when the transaction allows
it.

The app's Mode 1 graft work is an optional task inside Deploy → Prepare. It
enumerates the selected VBMETA's chain descriptors and verifies any generated
image before it can be written.

The fresh-install operation is different: it uses Deploy's **fresh-install**
lane and its **Provision** stage, then the `install` verb after the app confirms
the stock userspace `fastbootd` precondition. Do not use fresh-install `install`
as a substitute for the post-OTA inactive-slot action.

If the action is forgotten, the new slot carries a stock ABL. The GBL exploit is
absent, so BDS is not loaded and the device boots stock and unhooked. Nothing is
bricked by that omission. Run the inactive-slot flow while still in the known-
good slot and reboot again. Canoe does not provide a slot-switch action; to boot
the other slot, switch it outside Canoe with the platform fastboot tool, for
example:

```bash
fastboot set_active b
```

The app's status strip reports the active slot and BDS version when answered;
otherwise it says **Unknown** and the OTA operation must wait for reliable
metadata.

## Evidence and signer limits

Deploy's Review/Action stages review a `mode.plan` before commit. For a Mode 2
transition they may use `vbmeta.inspect`, `vbmeta.header`, and `vbmeta.check`;
these operations inspect evidence and do not themselves flash an image. If a
selected ABL or VBMETA image is supplied, it must be an exact, non-empty file
and is a derivation input only, never an implicit flash payload. `abl.verify`
can check a supplied ABL digest.

A successful Mode 2 derivation means only that vbmeta parsed and carries a
signature and public-key blob. No tool can prove which key is the OEM's. The
automatic safeguard is detection of a changed public-key digest relative to the
installed generation. A change is expected when moving to or from a Custom ROM;
follow the app's explicit signer-change acknowledgement and pass
`--allow-new-signer` only when that change is intended.

## Universal SCM safeguards

Modes 0, 1, and 2 best-effort suppress TrustZone fuse and anti-rollback SCM
requests during launch and refresh. This prevents further advancement only; it
cannot undo a blown fuse or lower an existing rollback floor. If the SCM
protocol is unavailable, launch continues and records `hooks-armed ... scm=0`.

## Xiaomi

Xiaomi fixed the GBL vulnerability in version **300**. As of version **306**,
XBL can still boot an older ABL to load `efisp` indirectly. Check the ABL
anti-rollback version before updating and test an OTA on a non-critical device.
A changed ABL that is incompatible with the device can still cause a hard brick;
the pre-reboot action does not make an incompatible vendor ABL safe.

Use a package freezer such as Hail when appropriate. Do not install an OTA
without confirming that the vulnerable ABL and the target firmware are
compatible.

## OnePlus

Newer OnePlus builds fix the loader path. Keep a vulnerable older ABL in the
partition and use **Install to Inactive Slot / OTA** after each OTA, before
rebooting, so the patched loader tracks the firmware generation. Builds through
`16.0.5.7xx` and below are vulnerable; newer builds may be fixed. Check the
device and wait for a tested result.

## Anti-rollback caution

If future firmware burns ABL anti-rollback versions, consider avoiding the OTA
or updating only HLOS. To identify HLOS images, extract `payload.bin` and look
for the `AVB0` header in each image before choosing partitions to flash.
