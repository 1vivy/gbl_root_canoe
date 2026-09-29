---
name: canoe-usb-host-mode-bringup
description: "Diagnose Canoe USB host mode on Qualcomm UEFI when a stick will not enumerate: role switch, DXE stack, census, VBUS and XHCI port evidence, all from UsbTools."
---

# Canoe USB host-mode bring-up

## Where this lives now

**The BDS does not acquire USB host mode.** `LinuxLoader.c` leaves the USB core exactly as inherited and records why: the vendor mode switch works and XHCI comes up, but nothing sources VBUS, because the Type-C/PMIC layer is never initialised on the ABL path and the charger DXE that would initialise it cannot start without a DPP provider this firmware does not carry. Probing that stack cost several unbootable devices. The census and the host attempt therefore live in the standalone `UsbTools.efi`, where they are one explicit operator action and a fault costs a tool run instead of the boot menu.

So the BDS menu has no USB-diagnostics row and no host-acquisition step. What it does have is `Advanced >` → **Android EFI tools** (browses `<boot-root>/tools/`) to launch a staged tool, the `[E] ` prefix on any volume that is *already* a USB volume (`SfbIsUsbVolume`), and **USB Mass Storage** for exports. Stick rows can only ever come from volumes the firmware already enumerated.

## Use when

Use on OnePlus 15 / SM8850 or similar Qualcomm UEFI when a USB stick must enumerate under UEFI, a role switch succeeds but no USB volume appears, or `PciIo`, `Usb2Hc`, `UsbIo` and `BlockIo` counts reveal a missing host-driver layer. Run `UsbTools.efi` before changing BDS.

## Procedure

1. **Build and stage the diagnostic tool.** Source is `submodules/uefi/edk2/AndroidToolsPkg/Application/UsbTools/`. Build with `make -C submodules/uefi tools`; artifact `submodules/uefi/build/UsbTools.efi`. Stage it as `tools/UsbTools.efi` on the boot root (the FAT container), or push it through a live export. Verify SHA-256 across an unmount/remount: pushing while persist is unmounted writes the recovery ramdisk and can hash-match the wrong copy.

2. **Stage the driver stack in dependency order.** The tool loads `\efisp\usbhost\<name>` first and falls back to `\usbhost\<name>`, both absolute on the volume the tool itself was launched from — with the boot root now being the container's filesystem root, `\usbhost\` is the path that exists and the `\efisp\` form is the legacy layout. Order is exactly (`mUtDriverStack`):

   | Driver | Consumes | Produces / census |
   |---|---|---|
   | `UsbPwrCtrlDxe.efi` | platform | `EFI_USB_PWR_CTRL_PROTOCOL` (VBUS control) |
   | `XhciPciEmulation.efi` | controller MMIO | `gEfiPciIoProtocol` / `pciio` |
   | `XhciDxe.efi` | PciIo | `gEfiUsb2HcProtocol` / `usb2hc` |
   | `UsbBusDxe.efi` | Usb2Hc | `gEfiUsbIoProtocol` / `usbio` |
   | `UsbMassStorageDxe.efi` | UsbIo | `gEfiBlockIoProtocol` / `blkio` |
   | resident FAT driver | BlockIo/DiskIo | SimpleFileSystem / `sfs` |

   `UsbPwrCtrlDxe` comes first deliberately: the vendor host init asks for VBUS *off*, and XhciDxe's automatic re-enable in `XhcGetRootHubPortStatus` is `#if 0` in the vendor source, so nothing turns bus power on implicitly. That DXE lives in the device's own `uefi_a` and is not dispatched on the Android boot path. `UsbConfigDxe` is already resident and provides the role-switch protocol.

   Sources: per-device Project-Silicium `Device-Binaries`, otherwise `BOOT.MXF.*/buildpath0/boot_images/boot/QcomPkg/Drivers/`. `UsbBusDxe` and `UsbMassStorageDxe` are upstream `MdeModulePkg`. Vendor builds need `--no-relax`, `--apply-dynamic-relocs`, `-Ttext=0x0`, `FUSE_LD=lld` and `-fno-stack-protector`. Sparse clones can hide tracked drivers; check:

   ```bash
   git ls-tree -r --name-only HEAD -- <codename> | grep -iE 'usb|xhci'
   git sparse-checkout add '/<codename>/QcomPkg/Drivers/XhciDxe/'
   ```

   Confirm the blob lineage against an on-device sibling: `llvm-objdump -f blob.efi` (expect `coff-arm64`), then `llvm-objdump -s -j .data blob.efi` — healthy vendor DXEs had 24–45 distinct pointer-like 8-byte slots, while one distinct value indicates lost dynamic relocations.

   Loading alone creates no child handles. Each controller handle must be offered to the bindings with `ConnectController`, and the handle XhciDxe finishes installing *during* that pass has never been offered to `UsbBusDxe`, so a second pass over the `Usb2Hc` handles is required. `XhcDriverBindingSupported` returns `EFI_UNSUPPORTED` without PciIo.

3. **Switch mode with both required operations.** `QCOM_USB_CONFIG_PROTOCOL` is `e722b03f-b250-42ce-8ebd-5bd51812d037`.
   1. `ToggleUsbMode(This, CoreNum)` stops the old mode and pins static config to the other mode; state becomes `USB_INVALID_MODE` (`0x10000`) and a second toggle there returns `EFI_NOT_READY`.
   2. Signal `gInitUsbControllerGuid` (`1c0cffce-fc8d-4e44-8c78-9c9e5b530d36`) with `CreateEventEx(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, dummy notify, NULL, &guid, &ev)`, then `SignalEvent` and `CloseEvent`. The callback starts the pinned mode.

   Neither half works alone. Restore with the same two steps in reverse — the tool always restores device mode before returning. Never leave the core invalid, or device-mode/MSD restart can fail `Not Found`. The pin is runtime-only and a reboot clears it.

4. **Use the matching protocol instance, and only the new handles.** Enumerate UsbConfig handles and select the instance whose `CoreNum` matches; `LocateProtocol` may return another core, the vendor reads core from `This`, and the wrong instance can fault at the first MMIO capability read. Snapshot handles before start and offer only the newly created delta to `ConnectController`; a stale peripheral handle shares the new `modeType` and double-binds the shim. Never call naked `StopController`: `StartController` stops the old mode internally, while an external stop faulted the device.

5. **Respect vendor ABI limits.** `GetCoreCount` is `OUT UINT8 *`, not UINT32; cap scans to it. Do not call `GetUsbMaxHostCoreNum`, or `GetSupUsbMode`/`GetUsbVbusStatus` on a stopped or invalid core; those froze hardware. The shipped census quiesces to exactly the calls the BDS has already exercised on this device class.

6. **Do not trust `StartController` success.** A bare `StartController(core, XHCI)` can perform PHY bring-up yet leave `modeType=DEVICE` because the static pin wins; asking for `DEVICE_SS` under a pin can still land in XHCI. Poll for the protocol and accept ownership only when `usb2hc > 0`. If the switch changed mode but no Usb2Hc appeared, reverse the switch and disown the core.

7. **Read the census by first zero.** It counts protocols with `LocateHandleBuffer(ByProtocol, …)`, reports every UsbConfig instance's `rev`/`core`/`mode`/`always` plus which vtable members exist, and prints each stage live with `gBS->Stall(120 ms)` because a faulting run may never flush DEBUG:

   - `usbconfig=N usb2hc=N pciio=N usbio=N` and `blkio=N sfs=N driverbinding=N usbfn=N`
   - `usbconfig=0`: no `UsbConfigDxe`; the census says host mode is unreachable.
   - `pciio=0` with `usbconfig>0`: supply the PCI-emulation shim.
   - `usb2hc=0` with `pciio>0`: supply the XHCI driver.
   - `usbio=0` with `usb2hc>0`: no `UsbBusDxe`, or no electrical enumeration — check power first.
   - `blkio` unchanged with `usbio>0`: supply `UsbMassStorageDxe`.

8. **Diagnose power and Type-C after XHCI exists.** `UsbConfigPortsQueryConnectionChange` reports DFP attach only after a successful `GetTypeCPortStatus`; on error it suppresses the failure and later assumes UFP. Look for `GetTypeCPortStatus returned ERROR` and `UsbPwrCtrlLib_ValidateRequest Invalid Port Index`. `UsbConfigPortsSetSimulate(HOST_ATTACHED)` changes software flags only. Project-Silicium's propagated Type-C/PMIC patches overwrite an `orr` status, force a config branch, and NOP the second/third Type-C backend errors because "Pmic dosen't Init Correct" — target-specific prior art, not a fix. The vendor host init disables VBUS and XhciDxe's re-enable is `#if 0`, so the lever is `UsbEnableVbus` or `EFI_USB_PWR_CTRL_PROTOCOL` (`e07df17e-e79e-4150-9378-50623a14994a`): no `This` pointer — a plain vtable of standalone functions — with `SetVbusSourceEn(UINT8, BOOLEAN)` at `+0x50` and `GetVbusSrcOkStatus` at `+0x18`. The attempt prints `usb power control: rev=… srcok=… vbuson=…` and each port's `srcok before/after`, so a port that will not enable is visible rather than inferred.

9. **Read XHCI port state directly if needed.** Resolve base via `GetUsbHostConfig(…XHCI…, &CoreType)` then `GetCoreBaseAddr(…, &Base)`. Read CAPLENGTH at `+0`, HCSPARAMS1 at `+4` (MaxPorts bits 24–31), and PORTSC at `Base + CAPLENGTH + 0x400 + 0x10*port`; the tool prints `ccs=`, `ped=`, `pp=`, `pls=`, `speed=`. `CCS` bit 0 means electrically present and `PP` bit 9 means powered. `CCS=0` with a stick attached points to role or power; a powered OTG hub is the zero-code test. This is the one fact no protocol count can give and this firmware's XhciDxe will not print.

10. **Do not load the charger stack.** Recorded here so it is not retried: `GlinkDxe`, `PmicGlinkDxe` and `ChargerExDxe` all started and `ChargerCount` moved 0 → 1, but `QcomChargerDxeLA` — the one that runs `SchgInit` and fills the port table — answered `Device Error`. Its INF locates five protocols at entry under a `TRUE` depex; four are present and `gEfiDppProtocolGuid` (`42d430c0-ae55-4a6c-a795-c63e687a1549`) is absent, and `DppDxe` is in no `uefi_a` on this handset. That run also left the USB gadget wedged past a reboot, needing a hard power cycle. Getting further needs a stub DPP provider (which lets a charging application configure a battery from defaults — a hardware risk) or direct SPMI register writes.

11. **Drive it from the operator loop.** `fastboot boot BDS.efi` (RAM-only, gated), then **Advanced >** → **Android EFI tools** → `UsbTools.efi` → **USB Census (read-only)**, then **Attempt USB Host Mode** (Vol+ confirms; the fastboot link dies the moment the core flips, so unplug the PC and attach the stick first), then **Dump Report to logfs**. Mount the logfs volume and read `UsbToolsDump.txt`. The attempt screen stays as a transcript with one line per stage, so a fault leaves the stage name on the display.

12. **Build a deterministic two-volume test stick** — only observable once the port has power. Select `/dev/disk/by-id/usb-…`, verify `TRAN=usb` with `lsblk`, unmount, then create FAT32 GPT partitions:

    ```sh
    lsblk -o NAME,SIZE,TYPE,TRAN,VENDOR,MODEL,LABEL,FSTYPE,MOUNTPOINT
    ls -l /dev/disk/by-id/ | grep usb-
    udisksctl unmount -b "$D-part1"   # repeat for existing partitions
    ```

    ```sh
    sudo wipefs -a "$D"
    sudo sfdisk "$D" <<'EOF'
    label: gpt
    start=2048, size=33554432, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B, name="CANOEUSB"
    start=33556480, size=16777216, type=EBD0A0A2-B9E5-4433-87C0-68B6B72699C7, name="CANOEAUX"
    EOF
    sudo partprobe "$D"; sleep 2
    sudo mkfs.vfat -F 32 -n CANOEUSB "$D-part1"
    sudo mkfs.vfat -F 32 -n CANOEAUX "$D-part2"
    sudo blkid "$D-part1" "$D-part2"
    ```

    Re-run after a `partprobe` race; `lsblk` can cache. Seed both with `EFI/BOOT/BOOTAA64.EFI` = a loader that prints usage and returns `EFI_INVALID_PARAMETER` when handed no LoadOptions (`FdLoader` was that fixture and ships in no repository, so stage it by hand); CANOEUSB also gets `payload/fdloader.efi`, `payload/abootloader.efi`, placeholder `Image`, `initrd.img`, `dtb.dtb` and BLS entries. CANOEAUX receives only this boot file. That run proves USB `LoadImage`/`StartImage` while safely returning to a menu.

13. **Exercise BLS semantics** once the BDS can see the stick. Keys are `title`, `linux`, `efi`, `initrd`, `devicetree`, `options`; root-relative paths and exactly one of `linux`/`efi` are required. Preserve these filenames and marks: `10-fdloader.conf` valid EFI; `20-aboot.conf` EFI plus real options; `30-linux.conf` valid Linux; `40-linux-alt.conf` same Image with different options (dedupe guard); `50-broken.conf` both linux+efi (`bls-reject`); `60-missing.conf` absent payload (`bls-missing`); `70-rejects.conf` duplicate initrd plus devicetree (`rejected=2`); `80-notitle.conf` stem fallback; `90-distrokeys.conf` accepted Linux with `version`/`machine-id`/`sort-key`/`architecture` ignored; `README.txt` skipped; and `bls-truncated` as the overflow mark.

## Traps / failure signatures

- The BDS will not acquire host mode for you. A stick attached at the menu while USB is a fastboot gadget cannot produce a row; host work is one `UsbTools` run.
- `fastboot boot` and flashed `efisp` have different inherited handles (367 vs 363 observed); remeasure flashed.
- A bus-powered SSD can exceed VBUS. Retest with a low-draw stick or powered OTG hub.
- The phone cannot be a fastboot device and a USB host at the same time; missing rows before the cable swap are expected.
- `SfbScanBlsEntries` hides entries whose image file is absent (`bls-missing`); a missing `40-linux-alt.conf` means bad entry dedupe.
- Approval-gated `fastboot boot|flash|erase` must not be rerouted through an ungated mechanism.

## Verification

- Staged tool and drivers hash-match after unmount/remount; each PE is ARM64 with healthy `.data` pointers.
- The attempt transcript brings the stack up in dependency order (`pciio` → `usb2hc` → `usbio` → `blkio` → `sfs`) with no unexplained first zero, then ends in `verdict: usb2hc=… pciio=… usbio=… blkio=… sfs=…`.
- Every exit path restores device mode: `restore toggle-back: …` followed by a `restore settle pass` that shows `usbfn` back. `restore FAILED: no USB function after 3 passes` is the failure to report.
- `UsbToolsDump.txt` records every stage, the mode restore and the release; repeat the census on the flashed path, because a RAM boot inherits its launcher's environment.
