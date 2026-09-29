---
name: canoe-host-tooling-live-export-smoke
description: "Smoke-test Canoe host tooling against a live read-only boot-root export when release artifacts, paths, install targets, or device identity need hardware proof."
---

# Smoke-testing Canoe host tooling against a live export

Finds the defects only real hardware exposes while writing nothing to the device. Three were caught this way in one session: a stale BDS version stamp, an install that targeted the volume root instead of the boot root, and an access probe that lied on Linux.

The load-bearing idea: **capture ground truth over adb first**, then make every host-tool answer face that oracle. Without it you cannot tell "empty result" from "broken path" — both look like success.

Boot-root tooling today (`gbl_root_canoe/tools/`): `canoe-bootmgr` (config, entries, defaults, BLS and prepared loaders on an already mounted boot root), `canoe-provision` (create/inspect/remove the container on supplied persist storage), `canoe-image` (inspect and prepare supplied images), and the shared `canoe-fs` library they build on. `canoe-ext4` is a bounded ext4 reader/writer retained for reference and absent from b5 release and default test paths. The interactive `canoe` CLI and `tools/canoe-host/` are retired: do not revive them and do not expect `--source`, `source detect`, or a `slot` subcommand on any command. Mounting and device access are external to these tools.

## 1. Oracle first

`adb reboot recovery`, then read persist read-only. Never write `persist` while Android runs.

```bash
adb shell 'ls -l /dev/block/by-name/ | grep -iE "persist|efisp|abl"'
adb shell 'mkdir -p /tmp/p && mount -o ro /dev/block/by-name/persist /tmp/p && ls -l /tmp/p/efisp.fat'
adb shell 'mkdir -p /tmp/c && mount -o ro,loop /tmp/p/efisp.fat /tmp/c && ls -la /tmp/c'
adb shell 'cat /tmp/c/canoe.cfg; ls -1 /tmp/c/tools/; ls -1 /tmp/c/loader/entries/'
adb shell 'umount /tmp/c; umount /tmp/p'     # always, before rebooting
```

The boot root is the FAT16 container `persist/efisp.fat`; its filesystem root is the boot root, and paths there never carry the legacy `efisp/` prefix. Record the config grammar (`timeout N` alias vs `menu-mode`), loader names (`boot.efi` pre-b2, `boot_a.efi`/`boot_b.efi` current), tools list and BLS entries — those are the expected answers for every step below.

## 2. Prove the artifact you are about to boot is current

`CANOE_VERSION` reaches the BDS as a `-DSFB_BDS_VERSION` compiler flag, and EDK2 does not track flags as dependencies. Check the bytes, not the build log — both encodings, because they are two different claims:

```bash
strings -a -e s BDS.efi | grep -F "$VERSION"   # CHAR8 -> the `canoe-bds` fastboot variable
strings -a -e l BDS.efi | grep -F "$VERSION"   # CHAR16 -> the menu credit line
```

Missing? Rebuild through `canoe-bds-rebuild`; its `build` target already deletes the EDK2 output tree, so this is cheap to redo.

## 3. RAM-boot and confirm identity

From recovery, `adb reboot bootloader` lands in the **BDS's own** Super Fastboot (serial `0000000000000000`, `product=canoe`), not the OEM ABL. Baseline `fastboot getvar canoe-bds` = what is flashed. Then:

```bash
fastboot boot submodules/uefi/build/BDS.efi     # gated action; RAM only, power cycle reverts
```

Re-query `canoe-bds`: it must report the **new** version, which is what proves your image is the one running. A successful host response alone does not prove the new BDS ran, because the command is acknowledged before `LoadImage`/`StartImage`.

**The first fastboot command after the gadget re-enumerates can answer `< waiting for any device >`.** Retry 2–4 times before concluding anything; treating one miss as "not Super Fastboot" aborts a valid run.

## 4. Export, then sweep read-only

```bash
fastboot oem mass-storage:boot-root   # manual export: 1209:ca0e, your OS mounts it; targets also persist, logfs
lsblk -dn -o NAME,SIZE,VENDOR,MODEL   # expect the container as a small FAT volume
```

The manual export enumerates as ordinary mass storage. The app-driven managed export enumerates as a vendor-class device (`1209:ca0f`) with interface-scoped WinUSB descriptors, so no OS filesystem driver claims it and the app is the exclusive writer; there is nothing to mount, eject or race against. Raw `persist` export is for provisioning or deliberate offline ext4 work only.

Run every read verb against the mounted root and compare it with the oracle:

```bash
canoe-bootmgr --boot-root /mnt/x config show
canoe-bootmgr --json --boot-root /mnt/x entry list
canoe-bootmgr --boot-root /mnt/x bls list
canoe-bootmgr --boot-root /mnt/x loader show --slot a
canoe-ext4 list /dev/sdX /            # helper-level ground truth to arbitrate
```

Run an access probe both unprivileged and elevated. If the two agree about whether the process can read or write the source, the probe is lying: mode bits and a successful `stat` answer a different question than "can this process open it".

Disagreement between two verbs on the same source (one `ok:true` with an empty list, another erroring) is itself a finding: it usually means a path resolves to nothing.

## 5. Prove where an install would land — offline, never on the device

Do **not** test a write path on hardware. Build a persist-shaped image and look:

```bash
truncate -s 128M /tmp/persistlike.img && mkfs.ext4 -q -F -b 4096 /tmp/persistlike.img
canoe-provision inspect --persist-image /tmp/persistlike.img --ext4-helper ./canoe-ext4
canoe-provision create  --persist-image /tmp/persistlike.img --ext4-helper ./canoe-ext4
sudo mount -o loop /tmp/persistlike.img /mnt/p && sudo mount -o loop /mnt/p/efisp.fat /mnt/c
canoe-bootmgr --boot-root /mnt/c loader install --slot a --from ./prepared
canoe-bootmgr --boot-root /mnt/c entry set --id android-a --title 'Android A' \
  --image boot_a.efi --role other --mode 1 --default
ls -1 /mnt/p; ls -1 /mnt/c; ls -1 /mnt/c/tools 2>/dev/null
```

`create` refuses an existing `efisp.fat`; `loader install` validates the ARM64 PE, 120-byte `.gm2p` and 256-byte `.tzmap` triplet and publishes the sidecars before the EFI file (add `--replace` only deliberately). Anything landing at the volume root instead of inside the container is a wrong-target write into a vendor partition — and it reports `ok:true` either way. Unmount both before touching the image again.

## 6. Ending the session

Volume Down on the device is the only exit, and this is measurable rather than folklore: during an export `fastboot devices` is **empty** (mass storage fully replaces the gadget, so no `oem` command can reach the BDS), and the export is not a device the host can eject — the removable bit follows the LUN, so a boot-root container export presents as removable media while a raw `persist`/`logfs` export keeps fixed-media identity. Do not send SCSI `START STOP UNIT` at a vendor MSD to force it.

To harvest `/proc/bootloader_log` marks the session must continue **into a kernel**: end the export, pick the Android row, and because the pending reason is still recovery the boot lands in recovery with adb up. A session ending in Super Fastboot loses every mark; Canoe's own `bds-N.log` is the way to keep evidence without a handoff (see `canoe-bds-boot-entry-verification`).

## Never

- Never write to the device to test a write path; the offline image answers it.
- Never treat an empty list as absence when the probe might not have looked — check with the helper directly.
- Never flash raw `efisp` to test a BDS change; RAM-boot.
- Never let two writers touch one boot root: no live `persist` export concurrently with Android, and no editing the same container from another program during a manual export.
