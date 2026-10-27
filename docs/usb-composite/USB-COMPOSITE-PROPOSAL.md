# USB composite proposal — original note

The proposal below preserves the side-conversation response as written. Its evidence and limitations are assessed separately in [USB-COMPOSITE-CREDIBILITY.md](USB-COMPOSITE-CREDIBILITY.md).

---

**Yes—exposing Super Fastboot and managed storage together as a USB composite device looks feasible.** BOOT.MXF.2.5.1 provides useful infrastructure for this.

I checked the source: Qualcomm’s controller driver already iterates through multiple interfaces and configures their endpoints. This is stronger evidence than merely finding composite-device support in a header. [Controller implementation](/home/vivy/Projects/efisp-projects/BOOT.MXF.2.5.1/buildpath0/boot_images/boot/QcomPkg/Drivers/UsbfnDwc3Dxe/UsbfnDwc3Ch9.c:101)

The proposed shape would be:

```text
One persistent USB device
          │
   Shared USB dispatcher
          ├── Super Fastboot interface
          └── Managed storage interface
                  └── Selected backing storage
```

CBM could keep both interfaces claimed throughout its session. Selecting persist or `efisp.fat` would change the storage target internally, without replacing the USB device. WebUSB supports accessing individual interfaces of composite devices, subject to their driver bindings and permissions. [Chrome documentation](https://developer.chrome.com/docs/capabilities/build-for-webusb)

**The work is combining the existing drivers under one owner.** Today, the MSD driver configures USB enumeration and stops the controller itself; Fastboot also manages its own lifecycle. A shim cannot simply start both unchanged. It would need to own descriptors, control requests and event polling, then dispatch transfers to each function. [Current MSD lifecycle](/home/vivy/Projects/efisp-projects/BOOT.MXF.2.5.1/buildpath0/boot_images/boot/QcomPkg/Drivers/UsbMsdDxe/UsbMsd.c:479)

A resident UEFI component loaded before starting Super Fastboot could provide that ownership. I would favour adapting our drivers to it over transparently intercepting their existing controller calls.

There would still be a small, necessary storage rule:

- Fastboot queries can remain available while FAT is open.
- A conflicting flash requires finishing filesystem work and releasing its backing storage first.
- Raw persist and the FAT image inside it must not have simultaneous independent writers.
- Switching backing storage still requires flush/close/reopen, **but USB itself can remain connected**.

This directly addresses the transport churn. It would also reduce the pressure to capture everything merely because asking for another partition currently means losing MSD access. Caching would remain useful for speed and preparation.

The remaining qualification is endpoint behaviour on the actual firmware, shared event handling, and Windows/WebUSB bindings. It also only helps once our UEFI code is running; the initial transition from stock Fastbootd into CANOE-BDS would still involve reconnection.

**I think this is a credible firmware simplification to investigate before adding more browser-side transport machinery.** It preserves Fastboot and SCSI rather than requiring a new multiplexed wire protocol.
