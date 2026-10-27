# USB composite proposal — credibility assessment

Companion to [the original proposal](USB-COMPOSITE-PROPOSAL.md). This is a source-based feasibility assessment, not an implemented design or a hardware acceptance report. The side-conversation inspection made no firmware changes and performed no phone tests.

## Assessment

The proposal has a credible foundation: multiple USB interfaces are supported by the reference Qualcomm descriptor and endpoint code, and WebUSB is designed to claim individual interfaces. The strongest claim currently justified is **worth a bounded prototype above the resident USB controller driver**.

It is not yet justified to call this a small shim, promise elimination of every USB chooser, or assume the reference implementation exactly matches the target firmware. The substantial work is joining two independently owned device lifecycles and event loops, not constructing a composite descriptor.

## Evidence inspected

Paths below are relative to `efisp-projects/`. Function names identify the inspected code if line numbers move.

| Evidence | What it supports | What it does not establish |
| --- | --- | --- |
| [EFIUsbDevice.h](../../../BOOT.MXF.2.5.1/buildpath0/boot_images/sdk/QcomSdkPkg/Include/Protocol/EFIUsbDevice.h), `USB_DEVICE_DESCRIPTOR_SET` and `USB_DEVICE_TRANSFER_OUTCOME` | The API describes multiple interfaces and identifies transfer outcomes by endpoint. | That two existing clients can independently own the same protocol instance. |
| [UsbDevDesc.c](../../../BOOT.MXF.2.5.1/buildpath0/boot_images/boot/QcomPkg/Drivers/UsbDeviceDxe/UsbDevDesc.c), `UsbDevDescInitInterfaceInfoTable`, around line 449 | The descriptor implementation allocates and parses an interface table using `NumInterfaces`. | Correctness for every descriptor arrangement, alternate setting or resident driver version. |
| [UsbfnDwc3Ch9.c](../../../BOOT.MXF.2.5.1/buildpath0/boot_images/boot/QcomPkg/Drivers/UsbfnDwc3Dxe/UsbfnDwc3Ch9.c), `UsbfnDwcCoreSetInterface`, around line 101 | The controller implementation finds interfaces and configures their endpoints at USB 2 and SuperSpeed rates. | Successful concurrent Fastboot and BOT traffic on the actual phone. |
| [UsbDeviceDxe.c](../../../BOOT.MXF.2.5.1/buildpath0/boot_images/boot/QcomPkg/Drivers/UsbDeviceDxe/UsbDeviceDxe.c), `UsbDeviceHandleEvent`, around line 480 | Transfer notifications retain endpoint identity, and nonstandard setup requests can be surfaced to the client. | A ready-made dispatcher for independently written USB functions. |
| [UsbMsd.c](../../../BOOT.MXF.2.5.1/buildpath0/boot_images/boot/QcomPkg/Drivers/UsbMsdDxe/UsbMsd.c), `UsbMsdEventHandler`, `UsbMsdStartDevice`, `UsbMsdStopDevice` | MSD directly polls USBFn, configures enumeration and stops the controller. | That it can be loaded alongside Fastboot unchanged. |
| [FastbootMain.c](../../submodules/uefi/edk2/QcomModulePkg/Library/FastbootLib/FastbootMain.c), `FastbootUsbDeviceStart`, `HandleUsbEvents` | CANOE-BDS Fastboot starts its own USB device and polls its event stream; the handler contains endpoint-1 assumptions. | That adding another interface alone will service its transfers or avoid competing event consumption. |
| [Managed export documentation](../../../canoe-msd/docs/managed-export.md) | The current managed route uses vendor-class BOT, distinct from ordinary OS-mounted MSD, and documents backing flush and eject completion. | Composite Windows binding: the documented Microsoft descriptors currently target interface 0 only. |

The source investigation is intentionally limited. It did not compile a composite configuration, audit every synchronous Fastboot command or inspect the resident controller binary for equivalence.

## Platform boundary

The existing [canoe-usb documentation](../../../canoe-usb/README.md) records that BOOT.MXF.2.5.1 supplies Bonito, Eliza and Pakala platform sources, while the OnePlus 15 target uses SM8850/Kaanapali firmware from a later BSP. That compatibility gap must remain explicit; the reference source is not proof of the resident implementation.

Prefer keeping the phone's resident USBFn/controller, PHY and platform configuration layers. Place the proposed dispatcher above their exposed protocol. Check the live protocol contract and endpoint behaviour before relying on it. This proposal does not require changing the phone into USB host mode or replacing its platform USB driver.

The separate [canoe-usb host-role work](../../../canoe-usb/docs/decision.md) addresses a different problem. Its conclusions about loading XHCI and USB host drivers do not establish composite peripheral support.

## Proposed implementation boundary

Use one USB configuration with two interfaces and distinct bulk endpoint pairs:

- Super Fastboot retains its existing command protocol.
- Managed storage retains BOT/SCSI and exposes one selected backing target at a time.

One resident component owns controller start/stop, descriptors, endpoint-zero requests and event polling. Fastboot and storage become function handlers under that owner. The owner routes endpoint completions, dispatches interface-specific control requests and distributes bus reset/disconnect events to both functions.

This could live with the USB abstractions in `canoe-usb`, with the storage function remaining in `canoe-msd` and BDS supplying commands and backing-storage selection. Those are suggested boundaries, not a decision to reorganize the repositories.

Simply interposing wrappers on both existing drivers' Start/Stop calls is insufficient: they also consume events and maintain independent device-level state. A transparent proxy would have to virtualize those semantics too, potentially adding more complexity than explicitly adapting the functions.

Long-running Fastboot handlers must not starve the USB dispatcher. Multiple interfaces do not make existing synchronous firmware handlers concurrent or require introducing threads; cooperative servicing and serialization may suffice. Qualify that behaviour before promising simultaneous transfers.

## USB continuity and storage ownership

Keeping the USB device enumerated is distinct from keeping a filesystem open. A target change should drain outstanding storage commands, flush the filesystem and backing device, close the old filesystem, select the new target, then reopen and reread its capacity and geometry. The USB connection can remain intact if this lifecycle works correctly.

The storage interface can remain present while reporting no medium or not-ready during the change. Correct BOT/SCSI media-change handling, host caching and the nusb storage client's reopen behaviour still need implementation and testing. The phrase “change the storage target internally” in the original proposal describes the desired result, not an already available operation.

Only independent operations can overlap:

| While FAT is open | Policy proposed for qualification |
| --- | --- |
| Cached Fastboot properties or status | Allow if the handler is independent of the mounted storage. |
| Fetch/hash/write of another partition | Serialize underlying I/O as needed; audit each command's side effects. USB presence alone does not make every command safe. |
| Raw persist access that bypasses the live FAT view | Require release of the overlapping storage view. Even reads can observe an inconsistent snapshot during writes. |
| Replacing/deleting/resizing `efisp.fat`, or mounting raw persist independently for mutation | Require FAT close, backing flush and mapping release first. |
| Reboot or booting another payload | Finish storage work and release it before leaving the environment. |

Raw persist and `efisp.fat` alias storage. They must not become independently writable LUNs merely because USB can expose multiple functions. BDS must also relinquish any conflicting local filesystem view while CBM owns its export.

A successful flush must propagate through the filesystem, block transport and backing device; a disconnect is not a flush acknowledgement. There is no inherent requirement here for a whole-volume hash, a read-only remount after each write or durable session identity tracking. CBM can retain file/partition checks appropriate to the actual operation and use the drivers' synchronization contract.

## Host compatibility and expected benefit

[Chrome's WebUSB guidance](https://developer.chrome.com/docs/capabilities/build-for-webusb) explicitly describes composite devices and interface-level access. [Microsoft's composite enumeration documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/enumeration-of-the-composite-parent-device) describes how Windows recognizes and enumerates their functions.

Keep managed storage vendor-class so ordinary OS mass-storage mounting does not compete with CBM. Ordinary manual MSD remains a separate, deliberate presentation. Update and test interface-specific WinUSB descriptors for the composite configuration; copying today's interface-0 descriptors unchanged is insufficient. Platform permissions, another process claiming an interface and browser grant behaviour remain real constraints.

CBM should hold one shared USB device handle with separately managed interface claims. Independent Fastboot and storage wrappers must not each close or reset that shared device. Reconnection should require an actual disconnect or environment change, rather than merely navigating to work that uses the other function.

This can remove repeated Super Fastboot/MSD re-enumeration once CANOE-BDS is running. It cannot remove the initial stock Fastbootd-to-BDS reboot, survive a physical disconnect without reopening, or guarantee one chooser for all browsers and environments. Cache policy can then be based on reuse and local preparation rather than avoiding expensive transport transitions; caching itself does not become unnecessary.

## Bounded qualification sequence

1. **Confirm the resident contract.** Check available USBFn interfaces and endpoint configuration support on the target. Keep existing Super Fastboot available as the fallback. Do not replace the platform layer based only on the reference BSP.
2. **Demonstrate composite enumeration.** RAM-load an experimental BDS with Fastboot plus a harmless storage function backed by a RAM image. Verify interface descriptors, USB 2/SuperSpeed operation, Linux access and Windows WinUSB binding.
3. **Demonstrate one event owner.** Exercise interleaved Fastboot queries and RAM-backed BOT reads, resets, stalls, aborts and cable removal. Establish that neither handler consumes the other's events or starves it.
4. **Demonstrate target changes without re-enumeration.** Swap differently sized RAM-backed media after an explicit close/flush boundary. Verify fresh capacity, filesystem reopen and no stale host block cache. Record actual USB disconnect/connect events and chooser requests.
5. **Demonstrate synchronization and exclusion.** Test file writes, backing flush failures, delayed completions and rejected overlapping raw operations on disposable media. A failure must remain visible without automatically replaying writes.
6. **Qualify CBM with physical storage separately.** Under explicit device-write authorization, exercise persist provisioning followed by FAT file work, file readback, ordinary Fastboot compatibility and orderly reboot. Confirm that this reduces transitions without weakening write ordering or filesystem ownership.

Proceed beyond the prototype only if the controller and host combinations support it without a growing compatibility shim. A passing source review alone is not acceptance. If the implementation requires replacing the platform controller or rewriting both wire protocols, reassess its cost against the simpler single-owner transport-switching design.

## Recommendation

Prototype composite enumeration and shared event dispatch first, using RAM-backed storage. That is the cheapest decisive evidence. Preserve the current working transport path until the prototype demonstrates actual USB continuity and clean storage handoff. The proposal can simplify CBM, but that benefit should be demonstrated before committing to a larger firmware rework.
