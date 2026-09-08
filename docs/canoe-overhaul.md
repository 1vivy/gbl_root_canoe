# Canoe 7.0.0-b4 overhaul

Accepted 2026-09-08. This supersedes the all-in-one bootmgr and raw FAT
maintenance designs. Intermediate packages are not release candidates.

## Boundaries

`canoe-bootmgr` is the mounted-root CLI/library: config, entries, defaults,
BLS, prepared loader installation, format/path checks and checked file I/O.
It has no USB, ext4, image-helper resolution, GUI sessions, deployment evidence
gates, automatic partition snapshots or readback workflows. Retire `canoe`.

`canoe-image` owns existing image inspection, derivation, graft and vendor_boot
operations. `canoe-provision` creates/inspects/removes the fixed container on
mounted persist or offline ext4 images. Reuse native image tools and unchanged
libext2fs; do not fork e2fsprogs or add a Windows ext4 kernel driver.

The manager application's native worker owns reviewed deployment, userdata
assessment, durable operation records, snapshots, readback, retry/revert and
uninstall. Desktop, WebUI and the KSU installer use that same engine. Its OS
adapters own dependency resolution, permissions, native mounts and USB/partition
transport. Share command libraries, never a second configuration implementation.

## Storage contract

Raw efisp remains the whole BDS image. `/persist/efisp.fat` is fully initialized
32 MiB FAT16, 512-byte sectors, 2 KiB clusters, with 8 MiB persist reserve.
Generate a fixed empty template at build time. Routine maintenance uses native
FAT filesystems; remove runtime FAT extraction/reconstruction and its whole-FAT
journal. Raw aligned I/O remains for provisioning and partition operations.

Export the FAT LUN as removable media for ordinary native mounting, also outside
the GUI. Keep raw persist export separate. Bind volume identity to its actual
USB device, preserve unrelated files/OS metadata, reject busy or changed managed
inputs, flush native filesystems and release USB ownership in order. Never mix
mounted-file writes with raw FAT writes. Do not claim exclusive access against
arbitrary external filesystem writers.

BDS retains bounded extent mapping, owned FAT/USB lifetimes and logfs boot
evidence. Add explicit Save as default with staged configuration and validated
previous-config fallback; ordinary selection is one-shot. Audit filesystem
confinement, FAT name collisions and publication (FAT has no hard links).
Legacy `/persist/efisp` is ignored, not imported or silently removed. Reinstall
and recreate entries; legacy presence only informs installation history.

## Product requirements retained

- Durable recovery spans initial ABL/BDS provisioning and later changes; Revert
  is for incomplete operations. Successful receipts remain inspectable.
- KSU installer uses new MODPATH tools, volume choices with cancellation,
  timeout/release handling, manager-only default, active-slot preparation and
  explicit custom recovery. Missing inputs escape before writes. No automatic
  update deployment, reboot/format or installer picker bridge. WebUI uses pickers.
- Host A/B/Both are manual, independently prepared, inactive first. Only Android
  OTA defaults cross-slot candidate active / verification inactive; collapse
  mode/sources and retain manual file overrides.
- General's inline Prepare Mode 1 boot images action defaults active verification
  material and preserves installed mode; no provisioning/loader/tool updates.
- Settings opens Deploy-shaped uninstall: explicit stock ABL, active default,
  optional inactive with separate picker and unchecked shared image. Restore and
  verify ABL inactive then active; wipe/verify raw efisp; unmount/remove container.
  Keep graft/vendor_boot/userdata/module and unrelated persist content. Durable
  retry, and optional recovery reboot only after verified completion. Warn that
  uninstall does not make the system stock and a completely stock phone is
  required before attempting relock.
- One application userdata evaluator retains true-locked bootstrap/logfs
  provenance, Mode 0 boundaries, effective signer comparison for Mode 1/2 and
  OTA, downgrade uncertainty and separate AVB failures. Missing evidence is not
  proof that formatting is required. Capture host history before provisioning.
- Preserve replacement ABL revalidation and corrected wording, automatic Super
  Fastboot guidance, Android insets/metadata and bounded Back-to-General behavior.

## Delivery and acceptance

Checkpoint existing work, extract components/callers, connect mounted FAT and
firmware persistence, delete obsolete paths, consolidate workflows, update wiki
(including gbl-chainload and Canoe <=6.3.5 reinstall), then rebuild all packages.
Version/capability-gate the application worker; no legacy fallback.

Validate standalone CLI independence, native mounted FAT on Linux/Windows USB
and KSU/KSU Next, packaged installer choices and interruptions, OTA/Both and
preparation-only behavior, uninstall/recovery source drift, busy volumes,
disconnect/flush/eject failures, preserved unrelated contents, filesystem checks,
fresh BDS compile and ARM64/Windows package inventories. Agentic packaged
exploration is required in addition to focused tests. Old raw VHDX tests do not
prove mounted FAT acceptance. Commit locally in each affected repository; do
not push. Physical-phone writes/reboots/slot changes/format need separate consent.

## Implementation progress

- Reviewed USB backing-flush changes checkpointed as `65b66aa9`; producer
  `canoe-msd` is at `82b1a78`.
- Initial component extraction compiles: the public CLI has config/BLS commands;
  the application worker is `canoe-boot-manager/native/canoe-manager`. Config and
  file publication are shared. The interactive `canoe` wrapper is retired.
- Linux mounted FAT CLI tests pass, including preserved unrelated files, refused
  traversal and read-only publication failure. Worker native tests, Windows
  cross-check, GUI 515 tests/typecheck/build, Tauri 32 tests and module bootstrap
  checks pass. This is not full mounted-volume application acceptance.
- `canoe-image` now owns image preparation and inspection. The application
  supplies reviewed helpers through a resolver; standalone commands resolve only
  needed helpers. Image outputs reject input aliases and use unique temporary
  files. Worker/image tests and Windows cross-check pass.
- `canoe-provision` creates a build-time FAT template and provisions mounted
  ext4/offline persist images through native allocation or unchanged libext2fs.
  The worker shares the primitive while retaining its readback/receipts. Tests
  cover preserved legacy/unrelated files, existing/attached refusals, initialized
  extents and clean filesystem checks.
- The USB producer now follows LUN removability; BDS marks the contained FAT
  removable and preserves the physical parent. Producer identity/flush/eject
  tests, PE relocation verification, BDS host tests and a fresh BDS build pass.
  Native Windows mounted-USB acceptance remains to be exercised.
- Full mounted FAT integration, loader/BLS artifact
  commands and application workflow consolidation remain in progress.
- The Windows app last inspected reports b3. Do not launch a mixed intermediate
  package as b4; replace it only with the integrated, checked b4 build.
