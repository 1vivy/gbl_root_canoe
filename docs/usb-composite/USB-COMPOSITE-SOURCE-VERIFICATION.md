# USB composite proposal — source verification

Third note in the set, after [the proposal](USB-COMPOSITE-PROPOSAL.md) and [the credibility assessment](USB-COMPOSITE-CREDIBILITY.md). This one re-reads the cited code and reports what the source actually says. No firmware was built, changed or run, and no phone was touched.

Paths are relative to `efisp-projects/`. Function names identify the code if line numbers move.

## Verdict

The proposal's central technical claim holds, and the reference controller layer supports it more deliberately than the proposal argued. The proposal's estimate of the *work* is the part that is wrong, and it is wrong in a specific way the credibility note only half-caught: Fastboot and canoe-msd are not two clients of one protocol. They sit at two different layers, and each carries its own USB chapter-9 state machine. A composite means retiring one of them, not arbitrating between them.

## Confirmed, and stronger than the proposal claimed

The proposal cited `UsbfnDwcCoreSetInterface` as evidence that the controller "iterates through multiple interfaces". That function only searches the interface table for one match. The real evidence is its caller.

| Finding | Where |
| --- | --- |
| On SET_CONFIGURATION the driver loops over `NumInterfaces` and calls the per-interface endpoint setup for every interface in the configuration. Multi-interface is the designed path, not a side effect. | `UsbfnDwcCoreSetConfig`, `BOOT.MXF.2.5.1/.../UsbfnDwc3Dxe/UsbfnDwc3Ch9.c` |
| Each IN endpoint anywhere in the configuration is assigned its own TX FIFO index, and `GTXFIFOSIZ` is re-laid-out across the FIFO count the hardware reports in `GHWPARAMS3`. A single-function gadget would never need this. | `UsbfnDwcCoreInitEps`, `UsbfnDwc3Impl.c` |
| The endpoint lookup backing that allocation scans every interface of the configuration in both the high-speed and SuperSpeed descriptor trees. | `UsbfnIsEpInConfiguration`, `UsbfnDwc3Impl.c` |
| The max-packet-size fixup walks every interface and skips interface-association and class-specific descriptors correctly. | `UpdateEpMaxPktSize`, `UsbDeviceDxe/UsbDevSmImpl.c` |
| Alternate settings are not a concern. SET_INTERFACE accepts only alternate 0 and GET_INTERFACE always answers 0, which is all a two-function composite needs. | `HandleSetupRequest`, `UsbDevSmImpl.c` |

So the descriptor, endpoint and FIFO plumbing for a two-interface device is present in the reference BSP and is not the risk.

## Corrections to both notes

**The two functions live at different layers.** Fastboot uses the higher-level device protocol, whose implementation owns a chapter-9 state machine. canoe-msd locates the lower-level USB function I/O protocol directly, then runs its own state machine and its own enumeration call.

- `gbl_root_canoe/.../FastbootLib/FastbootMain.c` calls `StartEx` and `HandleEvent` on `EFI_USB_DEVICE_PROTOCOL`.
- `canoe-msd/pkg/build/staging/UsbMsd.c` calls `LocateProtocol` for `gEfiUsbfnIoProtocolGuid`, then `DevSmInitialize`, `ConfigureEnableEndpointsEx` and `StopController` on it.

There are therefore two chapter-9 implementations in play, one of which must be abandoned. The credibility note's "joining two independently owned device lifecycles" is right about the cost and understates the reason.

**Both functions currently occupy the same address space on the bus.** The storage descriptor declares one interface, interface number 0, two endpoints on bulk endpoint 1. Fastboot's event loop acts only on endpoint 1 and silently discards every other transfer notification. At least one function must move to interface 1 and endpoint 2, and if Fastboot stays the poller unchanged, storage completions are dropped without any error.

- `canoe-msd/pkg/build/staging/UsbMsdDesc.h` defines the bulk endpoint as 1; `UsbMsdDesc.c` declares one interface numbered 0.
- `HandleUsbEvents` in `FastbootMain.c` filters on endpoint 1.

**The device protocol is a singleton with a lossy event call.** No member of `EFI_USB_DEVICE_PROTOCOL` takes an instance pointer, so there is one implicit client and one drained event stream. Its event call also returns at most one event, and later branches overwrite an event set by earlier ones, so a transfer completion can be replaced by a state change or a class-request notification within the same call. A dispatcher built on this protocol has to tolerate that; a dispatcher built directly on the function I/O protocol does not inherit it.

- `EFIUsbDevice.h` in `BOOT.MXF.2.5.1/.../QcomSdkPkg/Include/Protocol/`.
- `UsbDeviceHandleEvent`, `UsbDeviceDxe/UsbDeviceDxe.c`.

**The Windows descriptor gap is confirmed and is one byte plus one section.** The managed compatible-ID blob declares exactly one function section, first interface 0. A composite needs a section for the storage interface as well, and the interface GUID property has to be answered per interface.

- `canoe-msd/pkg/build/staging/CanoeManagedDescriptors.h`.

## Defect found in the reference descriptor parser

`UsbDevDescInitInterfaceInfoTable` declares its length accumulator once outside the interface loop, and the callee adds into it rather than assigning. The walk pointer is then advanced by the running total instead of by the current interface's own length.

Two interfaces still parse correctly, because the only over-advance happens after the last interface and is never used. Three or more interfaces misparse. That is the exact boundary this proposal sits on.

- `UsbDevDescInitInterfaceInfoTable` and `UsbDevDescInitInterfaceInfo`, `UsbDeviceDxe/UsbDevDesc.c`.

This affects only the path that parses raw descriptor blobs, which is the path Fastboot uses. canoe-msd hands the lower layer a pre-built interface table and never enters it. If a future third function is ever wanted, this is a blocker on the Fastboot path; for two functions it is not.

Separately, the debug printer for SuperSpeed descriptors walks endpoints without accounting for companion descriptors, so its output for a multi-interface SuperSpeed configuration is not trustworthy. That is a logging defect only.

## What the source cannot settle

- The resident Kaanapali implementation. Everything above was read in `BOOT.MXF.2.5.1`, which ships Bonito, Eliza and Pakala only. The platform boundary already recorded in `canoe-usb/README.md` applies unchanged.
- How many IN endpoints and TX FIFOs the live controller reports. The allocator reads that from hardware at run time, so it is answerable only on the phone.
- Whether two cooperating function handlers can be serviced without one starving the other under real traffic.
- Host-side behavior: Windows composite binding, browser interface claims and chooser counts.

One incidental result: the embedded-debugger packet-size clamp that would otherwise leak a 128-byte limit onto every later endpoint in the walk is compiled out on Pakala and always returns false. It is harmless as shipped, and worth re-checking only if a later BSP re-enables it.

## Effect on the qualification sequence

The credibility note's six-step sequence stands. Two steps should be sharpened before anyone starts:

1. Its step 1 should confirm the live IN-endpoint and TX-FIFO count, not just interface support, since that is the one hardware quantity the composite depends on and the source cannot supply.
2. Its step 3, the single event owner, should be scoped as choosing a chapter-9 owner and porting the other function onto it. Written as "one event owner" it reads like arbitration work; it is a rewrite of one function's device layer.

The cheapest decisive experiment is unchanged: enumerate two interfaces with a RAM-backed storage function and prove interleaved traffic on a real phone.
