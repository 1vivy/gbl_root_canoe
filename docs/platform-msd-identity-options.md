# Platform USB MSD identity: options for trees without the bundled driver

Status: deferred. Recorded for upstream (superturtlee) use; Canoe itself ships
the source-built canoe-msd driver and does not need any of this.

## Problem

The resident Qualcomm `UsbMsdDxe` enumerates as `05c6:f000`. Stock Linux
`usb_modeswitch` data (`/usr/lib/udev/rules.d/40-usb_modeswitch.rules`,
"Siptune LM-75 / EWangshikong 4G") matches that ID, claims the interface,
detaches `usb-storage`, and knocks the phone back to fastboot. Windows and
Linux hosts without `usb_modeswitch` are unaffected, which is why the export
can look fine on one machine and fail on another.

The only change needed to fix this is the VID:PID. The removable bit
(`UsbMsdScsi.c:515-517` hardcodes `0x80`) and INQUIRY strings are cosmetic.

## Rejected: patching the loaded driver in place

Tried in `ad8d5882`, removed in `e2ac4a53`. The descriptors are `CONST`
(`UsbMsdDesc.h:51-52`) and the RMB byte is an instruction immediate; writing
either in the running image put the OnePlus 15 into QUSB_BULK dump mode on the
host's first INQUIRY.

A memory dump of the loaded image is also not reloadable: it is relocated,
in memory layout, and its `.data` holds post-entry runtime state.

## Options, in order of preference

1. **Reload a patched copy read from the firmware volume.** Take the resident
   driver's `EFI_LOADED_IMAGE_PROTOCOL` (`FilePath` = FV file GUID,
   `DeviceHandle` = FV), read the pristine PE via
   `EFI_FIRMWARE_VOLUME2_PROTOCOL.ReadSection(EFI_SECTION_PE32)`, patch the two
   `12 01 … c6 05 00 f0` device descriptors in the buffer (same anchors as
   canoe-msd `tools/msd_variant.py`), `LoadImage`/`StartImage` it, select the
   new `EFI_USB_MSD_PROTOCOL` by handle diff. Soft fail: any anchor count
   mismatch or FV read failure → use the resident driver unchanged. No runtime
   state, no writes into firmware memory. Unverified: whether FV2 for that
   volume is still installed on the ABL path — prove with a read-only probe
   (ReadSection size + SHA-256 against canoe-msd `calibration/`).
2. **Descriptor-only hook on `EFI_USBFN_IO_PROTOCOL.Transfer`.** Fallback if
   option 1's probe fails. The vendor driver re-locates usbfn on every
   `StartDevice` (`UsbMsd.c:574`), sends everything through one `Transfer`
   call (`UsbMsdXfer.c:53`), and copies descriptors into writable
   `AllocateTransferBuffer` memory first (`UsbMsdSmImpl.c:376`). Rewrite
   VID:PID in an EP0 IN payload starting `12 01` with `05c6:f000` at offset 8;
   pass everything else through. Install before `StartDevice`, restore after
   `StopDevice` and on every error path (fastboot shares usbfn). First check
   the protocol struct is not `EFI_MEMORY_RO` via
   `gDS->GetMemorySpaceDescriptor`. Do not extend to INQUIRY rewriting: it
   needs cross-transfer state and partial-transfer handling for a cosmetic
   gain.
3. **Host-side udev override** skipping `usb_modeswitch` for `05c6:f000`. No
   firmware change, but every Linux user must install it and it disables
   switching for the real modem with that ID.

## Out of reach for byte patches and hooks

Host-eject reporting, clean SCSI errors on failed writes (the Windows attach
hang), and managed mode (`1209:ca0f`) are driver logic. They require the
canoe-msd source build. For the platform driver, the write hang can be avoided
one layer up by a BlockIo wrapper that drops writes and reports success on
read-only exports.
