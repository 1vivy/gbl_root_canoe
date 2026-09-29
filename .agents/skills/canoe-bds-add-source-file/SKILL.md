---
name: canoe-bds-add-source-file
description: "Add a .c/.h file to Canoe's EDK2 LinuxLoader when INF wiring, GUID linkage, headers, host tests, library mappings, or the 250-line limit could break the build."
---

# Add a source file to Canoe BDS

## Use when

Use for `submodules/uefi/edk2/QcomModulePkg/Application/LinuxLoader/**` additions in `gbl_root_canoe`, or when the canonical build reports `undefined symbol: gEfi...`, `unknown type name`, or `call to undeclared function`. The build stops at the first bad file, so resolve the full checklist before paying a Docker round trip.

## Procedure

1. Match neighbouring source style and split along a real seam before any file exceeds 250 lines. Keep one statement per line; do not compress branches or declarations to game the ceiling.

2. Add every new `.c` and `.h` to `QcomModulePkg/Application/LinuxLoader/LinuxLoader.inf` `[Sources]`. Missing `.c` compiles nothing and fails later with undefined symbols for your own functions.

3. Scan the new code for every `g...Guid` symbol and classify it:

   | Symbol kind | INF section |
   | --- | --- |
   | Event-group and HOB-list GUID | `[Guids]` |
   | GUID passed to `LocateProtocol`/`HandleProtocol` | `[Protocols]` |
   | New library class | `[LibraryClasses]`, only after confirming the DSC maps it |

   Known entries: `gEfiRscHandlerProtocolGuid` → `[Protocols]`; `gEfiEventExitBootServicesGuid` and `gEfiHobListGuid` → `[Guids]`.

4. Reconcile symbols with headers before building:

   | Symbol | Header |
   | --- | --- |
   | `DEBUG`, `EFI_D_INFO` | `<Library/DebugLib.h>` |
   | `AsciiStrLen`, `AsciiStrCmp` | `<Library/BaseLib.h>` |
   | `CompareGuid`, `CopyMem`, `ZeroMem` | `<Library/BaseMemoryLib.h>` |
   | `AsciiSPrint`, `AsciiVSPrint`, `AsciiBSPrint` | `<Library/PrintLib.h>` |
   | `EFI_FILE_PROTOCOL`, `EFI_FILE_MODE_*` | `<Protocol/SimpleFileSystem.h>` |
   | `EFI_FILE_INFO`, `gEfiFileInfoGuid` | `<Guid/FileInfo.h>` |
   | `gBS`, `gST` | `<Library/UefiBootServicesTableLib.h>` |
   | `ReportStatusCodeExtractDebugInfo` | `<Library/ReportStatusCodeLib.h>` from MdePkg |
   | `EFI_RSC_HANDLER_PROTOCOL` | `<Protocol/ReportStatusCodeHandler.h>` |
   | `EFI_EVENT_GROUP_EXIT_BOOT_SERVICES` | `<Guid/EventGroup.h>` |
   | `gEfiHobListGuid` | `<Guid/HobList.h>` |

5. Include `<Pi/PiBootMode.h>` before `<Pi/PiHob.h>`; otherwise `EFI_BOOT_MODE` is unknown.

6. For variadic code, use `AsciiVSPrint` for a `VA_LIST` forwarder and `AsciiBSPrint` for the `BASE_LIST` returned by decoded status-code data. They are not interchangeable.

7. Check `QcomModulePkg.dsc` before using a new library. `SynchronizationLib` is not mapped here; `Interlocked*` lives there, not in BaseLib, and requires an explicit DSC mapping.

8. If split files share a helper, define it non-`STATIC` in one file and declare it through the shared header. Do not duplicate the helper or hand-copy per-file prototypes.

9. Add a host test when the logic is pure enough. Established shapes under `submodules/uefi/tests/`:

   - Pure parser: `$(CC) $(CFLAGS) -DSFB_HOST_BUILD -o $@ $^`.
   - Real EDK2 types: follow `test_linuxboot`, link production `.c` against fake `gBS`, GUIDs, and library functions with `$(HOOK_INCLUDES) -fno-builtin -fshort-wchar`.

10. Add the binary to the `test:` dependency list and execution list, and to `tests/.gitignore`. Add `-DNO_MSABI_VA_FUNCS` for variadic EDK2 code on a SysV host.

11. Verify in order:

   ```bash
   make -C submodules/uefi test
   # then run the canonical Docker build from canoe-bds-rebuild
   ```

12. Check `build/BDS.efi` size because the whole PE is flashed as a fixed-capacity raw partition image. Check every touched file against the 250-line ceiling.

## Traps / failure signatures

- `ld.lld: error: undefined symbol: gEfiRscHandlerProtocolGuid` means the GUID is absent from the proper INF section, not that the GUID does not exist.
- `[Guids]`/`[Protocols]` omissions compile successfully and fail only at link time.
- `<Pi/PiHob.h>` alone produces `unknown type name 'EFI_BOOT_MODE'`.
- Editor diagnostics such as `'Uefi.h' file not found`, unknown `UINT32`, `STATIC`, or `EFI_STATUS` are workspace include-path noise. The package build is authoritative.
- `Print()` writes `gST->ConOut` and is screen-only in ordinary EDK2 accounting; `DEBUG()` routes through status codes to the platform log. Captured Canoe environments may tee console output separately, but do not rely on that for durable logging.
- Missing `-DNO_MSABI_VA_FUNCS` can compile and then crash a host test at runtime.
- Host test binaries are build outputs and every one is listed in `submodules/uefi/tests/.gitignore`; a committed one made `make test` run the stale binary instead of rebuilding from the `.c` beside it, and aborted on CI with `CPU ISA level is lower than required`.
- A version change alone does not invalidate objects. `CANOE_VERSION` must match `version.mk` (a differing command-line value is refused); use the canonical rebuild workflow.
- Splitting a file can silently lose an include that the old half provided.

## Verification

A complete change has: every source in `[Sources]`; every referenced GUID in `[Guids]` or `[Protocols]`; every library mapped by the DSC; host binaries registered and ignored; host tests passing where applicable; the canonical AArch64 `-Wall -Werror` compile and link passing; a present, size-checked `build/BDS.efi`; and no touched source above 250 lines.