# Canoe LinuxLoader/BDS working agreement

## Responsibility

This directory is the owner-controlled Canoe boot manager. `LinuxLoaderEntry` initializes the BDS, discovers boot volumes, reads configuration, presents the menu, launches EFI children, or enters fastboot. It is a UEFI application running at non-secure EL1; it does not implement XBL, TrustZone, or the final kernel handoff.

Keep responsibilities separated:

- `SuperFbConfig*`: parse and explicitly save `canoe.cfg` with validated fallback.
- `SuperFbFat*`: install private FAT/EXT4 drivers and classify boot volumes.
- `SuperFbEntries*` / `SuperFbBrowser*` / `SuperFbMenu*`: discovery, display, selection, and dispatch.
- `SuperFbLaunchPolicy*`: one child launch lifecycle.
- `Hook/`: narrowly scoped managed-ABL projections and restoration.
- `SuperFbMassStorage*` / `SuperFbMsdVariant*`: explicit USB export and bundled-driver selection.
- `Generated/`: build output only.

## Storage model

Never conflate these locations:

- raw `efisp`: contains this whole `BDS.efi`; hide its Block I/O handle during every managed ABL launch to prevent recursive re-entry.
- ext4 `persist/efisp.fat`: bounded FAT16 boot root (8–256 MiB in 8 MiB steps), containing configuration, EFI images, sidecars and the current Android handoff record. The FAT geometry and complete mapped range are validated against the actual file size; original 32 MiB containers remain supported. Legacy `persist/efisp` is ignored.
- FAT32 removable media: separate discovery/browser roots; never inherit managed boot-root policy implicitly.

`last-boot` is cleared on BDS entry, before launch, and on child return/menu/fastboot re-entry. Only a managed Android handoff publishes a checked CNLB record in the canonical container. Logfs retains debug logs only, never launch evidence. An unavailable container means no record; an accessible record that cannot be invalidated blocks managed handoff while leaving menu and Super Fastboot available.

BDS may explicitly Save as default with staged config and validated previous-config fallback; ordinary selection is one-shot. Image installation and application recovery belong to host/device tools. Persist is a boot volume only through its validated `efisp.fat` container.

## Managed launch invariants

Managed paths are exactly the live/backup ABL paths recognized by `SfbIsManagedAblEntry`; do not broaden that set accidentally. For a managed launch:

1. Parse and validate the exact adjacent `.gm2p` and `.tzmap` sidecars.
2. Prepare only the requested mode's policy.
3. Arm the universal efisp recursion guard in every mode.
4. Preload requested drivers without managed hooks left active.
5. Temporarily adjust image-security handling only around the intended `LoadImage` operation.
6. Restore image security before `StartImage`.
7. On every error or child return, disarm all QSEE/SPSS/SCM/VerifiedBoot/BlockIo wrappers and return to a clean menu/fastboot state.

Mode 0 remains honest-unlocked and installs no managed projection beyond the efisp recursion guard. Mode 1 and Mode 2 must obey the configured lock policy: either reject the launch or visibly downgrade to a fully honest-unlocked path, never continue with a partial projection or widen hooks to unrelated EFI images.

Sidecar wire formats are ABI:

- GM2P: exactly 120 bytes.
- TZ map: exactly 256 bytes, sorted unique command records, known version/flags, zeroed reserved and unused bytes.

Update producer and consumer tests together before changing either format.

## Image and driver loading

- Always distinguish authorization from PE parsing. Successful `LoadImage` is not proof that an image belongs to a managed generation.
- A returned child is a normal failure/re-entry path; clean up before reporting it.
- `DRIVER.LIST` entries are relative to the selected volume's virtual root. Connect controllers only after all requested drivers are started.
- The embedded USB mass-storage DXE is untrusted binary input until its MZ/PE form, AArch64 architecture, identity, and relocation slots are verified by the producer project.
- Current `SuperFbMsdVariant.c` attempts any nonempty embedded MZ image and falls back after load/start/protocol failure. Do not claim a resident-marker compatibility gate unless code and tests implement one.
- Exporting live `persist` requires a dedicated confirmation. Always stop the gadget and release its assigned Block I/O before returning to fastboot/menu.

## Tests and build proof

Run the host harness that covers the changed contract:

```sh
make -C submodules/uefi/tests test
```

The harness maps production source directly into tests for config parsing, volume classification, entry/launch policy, hook installation/restoration, DeviceInfo, GM2P, and TZ-map behavior. Tests must exercise the observable failure path and prove restoration, not only the successful rewrite.

After any C/H/INF change, complete the canonical `gbl_builder:latest` compile/relink described by the parent instructions. A host harness cannot catch missing UEFI library classes, unresolved compiler helpers, incorrect PE relocation, or an omitted INF source.

## Code constraints

- Boot Services run on one non-preempted boot CPU here; preserve intentional depth counters and avoid importing unavailable atomic helpers.
- Use bounded buffers and overflow-safe size checks for every on-disk structure, path, and protocol payload.
- Do not retain pointers into caller-owned or temporary sidecar buffers after validation; copy validated fixed-size state.
- Protocol wrappers must be idempotent: repeated prepare/disarm and failed partial installation leave strict pass-through behavior.
- Keep polling paths allocation-free and log-free except for bounded state-transition markers.
- Do not hand-edit `Generated/CanoeMsdVariantData.c`.
