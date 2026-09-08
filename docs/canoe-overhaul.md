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

## Implementation and validation

The component extraction and caller conversion are implemented. `canoe-bootmgr`
operates on supplied mounted roots and shares confined file/configuration code
with the manager; `canoe-image` and `canoe-provision` are separate commands.
Deployment, bootstrap continuation, preparation, uninstall and recovery use one
native application operation engine. Retired write verbs reject requests before
device access. Recovery records live outside persist and module extraction.

BDS uses the contained FAT root, explicit default saves and validated previous
configuration fallback. A deliberate BLS-only configuration remains valid and
does not resurrect deleted entries from the previous config. Read and close
errors propagate. Legacy ext4 directories remain untouched and are covered by
the reinstall documentation. The USB producer's source-built binary and its
relocations are verified and pinned; BDS has been rebuilt from current sources.

Actual Windows USB evaluation covers native FAT mounting, GUI UAC consent,
policy save, busy-volume refusal, flush/dismount/eject and detached readback.
The full native uninstall engine also runs against a fixed raw persist USB LUN
backed by ext4, using unchanged libext2fs only on its private offline copy.
USBSTOR's unsupported optional alignment property now selects conservative
64 KiB transfers; invalid metadata and unrelated I/O errors still fail. Both
selected ABL writes, raw efisp clearing, container removal, unrelated persist
contents and a clean filesystem were verified independently. A forced disconnect
retains completed partition receipts and reports cleanup as unfinished; retry
completes the same operation and independent filesystem checks pass.

Actual Linux GUI evaluation passes native FAT policy save, independent readback
and USB release. Full uninstall also passes with both selected ABL targets,
real fixed USB persist storage, container removal and independent filesystem checks. Native mounted-worker tests also cover source replacement,
confinement, busy unmount and attachment lifetimes.

The actual packaged standard KSU installer covers manager-only selection,
held-key release, cancellation, first deployment and module-only updates.
The WebUI preparation shortcut writes only the selected vendor_boot image and
preserves the installed Mode 2 entry. Android uninstall verifies its selected
ABL, clears raw efisp, removes the FAT container, and preserves unrelated persist
files, other images and the manager module. OTA source/mode defaults and native
DocumentsUI imports were inspected in the running WebUI.

KSU Next now has its own pinned kernel (33214), not just its manager on a standard
KSU kernel. It boots with SELinux enforcing, supplies root to an ordinary adb
shell and passes packaged timeout, held-key manager-only, cancellation and
first-deployment checks. This test-only x86 dispatcher build does not change the
host kernel or the ARM64 release.

All four package inventories pass architecture, current WebUI, shared EFI and
desktop helper-pin checks. The application's `e2e/RELEASE-READINESS.md` records
actual coverage and its limits. KSU Next also reaches actual recovery after
verified uninstall without formatting. Guest
results do not claim Qualcomm boot execution or KeyMint decryption coverage.
No physical-phone writes are part of this evaluation.

The final acceptance audit also exercises an actual packaged Next installer
write failure after container creation and boot-file publication. Its durable
record is visible in WebUI after the aborted extraction is gone; reviewed Revert
restores the operation, removes its new container and preserves original
partition hashes and unrelated persist. All seven privileged mounted lifecycle
checks pass on current sources. The application's `e2e/OVERHAUL-ACCEPTANCE.md`
maps the retained requirements to direct guest or shared-engine evidence and
records their hardware limits.
