# Boot-volume lifecycle and path audit — 7.0.0-b4

This is implementation status, not release acceptance. The FAT container change
is not yet connected end-to-end across the native backend, GUI, and packages.
Do not ship the intermediate mixture of new firmware and legacy host paths.

## Storage ownership

Raw `efisp` contains the whole BDS image. `/persist/efisp.fat` is the fixed
32 MiB boot volume. Legacy `/persist/efisp` content is neither a boot fallback
nor a migration source. User-selected input images are external OS paths;
config, BLS, and installed artifacts name paths within the boot volume.

The firmware container owner validates ext4 allocation and FAT geometry before
publishing Block I/O and connecting the FAT driver. It disconnects FAT and
withdraws the public protocol before lending a private Block I/O pointer to USB.
The USB lease retains that pointer until gadget stop, LUN release, and storage
flush/cache teardown succeed. A failed stage retains ownership and a subsequent
attempt retries only unfinished stages. Fastboot USB initialization and reconnect
are blocked while that lease remains outstanding.

Host tests cover partial gadget assignment, failed stop/unassignment/flush,
failed parent cache teardown after freeing the virtual disk, retained ownership,
and retry. The canonical AArch64 BDS build passes. This does not yet establish
behavior of the packaged gadget and FAT drivers on real hardware or guests.

## Logical paths

Rust `boot_path` now owns logical component validation for config images, BLS
references, staged artifact destinations, BLS filenames, and FAT tree conversion.
Firmware config and BLS readers use `SuperFbBootPath.h`. Both implementations
consume `submodules/uefi/tests/fixtures/boot-paths.tsv`.

The shared contract rejects parent/current-directory components, doubled or
trailing separators, drive letters, alternate data streams, Windows reserved
names, and names whose trailing spaces or dots change Windows interpretation.
One leading volume-root separator is accepted. Source image selection does not
use this contract: native file-picker paths remain ordinary OS paths.

Lexical validation is not filesystem confinement. `LocalDir` and artifact
staging still contain direct root joins; their descendant symlink/reparse-point
handling needs review before exposing them as a mounted boot-volume backend.
Do not substitute a one-time canonicalization check for owned directory access
and transaction lifetime. Existing ext4 confinement checks are separate and
must not be weakened when sharing operations with FAT.

## Remaining integration boundaries

- Native backend dispatch still selects Local/Ext4 and production Android paths
  still use the legacy root. Add the FAT boot-volume backend and its activation
  transaction before changing GUI defaults.
- GUI `src/lib/artifacts/paths.ts` contains `DEVICE_BOOT_ROOT` pointing at the
  legacy directory. The backend must return the owned mount root; the GUI must
  not independently discover or mount it.
- Consume `canoe-boot-volume=fat16-container-v1` before requesting the BDS
  `boot-root` export. Track ownership across UAC helpers and USB transitions.
- First allocation/final-name activation, exact readback, durable receipts,
  retries, and cleanup must share a lifecycle. Recovery information must live
  outside temporary extraction directories and the boot root being removed.
- Android needs an owned private loop/vfat mount and checked release before
  removal or export. No competing raw ext4 writer while Android owns persist.
- Review menu/entry/config/driver-list handles before volume handoff and child
  return. No retained root/file pointer may survive controller disconnect.
- Uninstall must release the owned FAT mount before deleting the container;
  selected ABL restoration and verification precede shared boot-root cleanup.
- Implement explicit BDS Save as default without making ordinary selections
  persistent. Keep its config recovery policy compatible with the Rust writer.
- Validate actual ext4 checksum/extent mapping and FAT-driver lifecycle against
  real generated filesystem fixtures, then packaged Linux, Windows UAC/USB,
  KSU, and KSU Next flows, including interruption and flush failures.

Physical phone writes, slot changes, reboots, and formatting are not part of
these source/build checks.
