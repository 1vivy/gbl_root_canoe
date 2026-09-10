# Canoe UEFI working agreement

## Scope

This subtree builds the owner-controlled AArch64 UEFI application shipped as `build/BDS.efi` and the optional AndroidTools EFI applications.

- Maintained product source: `edk2/QcomModulePkg/`, especially `Application/LinuxLoader/`, plus intentional `AndroidToolsPkg/` changes.
- Vendor/upstream support: the remaining EDK II package trees, BaseTools, and checked-in Conf templates. Change them only when the integration itself requires it; never broad-format or opportunistically modernize them.
- Generated: `edk2/Build/`, copied `edk2/Conf/`, `build/`, build logs, and `QcomModulePkg/Application/LinuxLoader/Generated/`.
- Binary ingress: `blobs/canoe-usbmsd.efi`, produced and verified by `../../../canoe-msd`. Never hex-edit it here.

## Execution contract

`LinuxLoader.efi` is packaged as `BDS.efi` and loaded raw from unauthenticated `efisp` by an authenticated vulnerable ABL. It executes as a UEFI application at non-secure EL1 with inherited Boot Services. It may consume installed protocols, allocate through UEFI services, load/start child images, and temporarily wrap protocol methods. It cannot assume EL2/EL3, secure-world ownership, unrestricted physical access, or that a protocol exists merely because its header is present.

The BDS never owns final OS handoff. A launched ABL/OS loader calls `ExitBootServices`; if a child returns, all Canoe state must be safe for menu/fastboot re-entry.

## Build invariants

`Makefile build` intentionally:

1. Copies the canonical Conf templates into EDK II.
2. Runs `embed_variant.py` to generate `CanoeMsdVariantData.c`.
3. Deletes the BDS `Build/RELEASE_CLANG35` output tree and previous `build/BDS.efi` before invoking a build command whose status is tolerated. This vendor build has missed header dependencies; relinking alone can retain incompatible old objects.
4. Requires a newly present `LinuxLoader.efi` and copies it to `build/BDS.efi`.

Do not weaken the clean-output rebuild or final existence check. A newly linked EFI is insufficient evidence when its constituent objects can be stale. Standalone AndroidTools use their own clean output tree.

The embedded mass-storage path has two valid states: a verified PE variant, or an explicit zero-size fallback to the resident platform protocol. A missing blob may be build-valid, but it is not release-valid when the requested feature is the bundled `1209:ca0e` driver.

## Required verification

After UEFI source or build metadata changes:

```sh
make -C submodules/uefi test
```

Then compile and relink from the repository root with the canonical builder:

```sh
mkdir -p /tmp/canoe-build
docker run --rm \
  -v "$PWD":/workspace -v /tmp/canoe-build:/out -w /workspace \
  gbl_builder:latest /bin/bash -lc \
  'make -C submodules/uefi build > /out/bds_build.log 2>&1; echo "exit=$?"'
```

Require `BDS built successfully: build/BDS.efi`. Never pipe the live build through a truncating filter. If the wrapper reports failure without a compiler diagnostic, compare source/object timing and force a real rebuild; do not increase timeouts or accept a stale binary.

After changing `blobs/canoe-usbmsd.efi`, first prove its identity and relocation health in `../../../canoe-msd`, then rebuild BDS and verify the generated array is nonempty. Device flashing is not part of build verification.

## Firmware coding rules

- Preserve AArch64 UEFI ABI and INF/DSC/FDF consistency; update every source list, library class, protocol GUID, and package dependency together.
- Check `LocateProtocol`, protocol pointer, method pointer, allocation, size arithmetic, and device path status before use.
- Avoid hidden libc/compiler-runtime dependencies. Stack protector, linker relaxation, dynamic relocations, and PE conversion behavior are build-contract concerns, not incidental flags.
- Keep all pre-`ExitBootServices` state in UEFI-owned memory and avoid work between memory-map acquisition and a child's eventual EBS call.
- Restore temporary protocol hooks on every exit path. Never leave wrappers armed while loading arbitrary `DRIVER.LIST` images, browsing, exporting storage, or entering fastboot.
- Keep hardware logs bounded and stable. `SFB: MARK` lines are device-debug contracts; add them at state transitions, not inside hot polling loops.
- Do not edit files under `Generated/`; change the generator or binary producer.
