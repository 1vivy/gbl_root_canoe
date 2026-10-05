# Read-only pstore inspection

Super Fastboot can inspect Linux ramoops records before another kernel changes
or clears them. The interface never writes, clears, erases, or cache-flushes the
persistent-RAM range.

## Why there are two discovery modes

`fastboot oem pstore info` asks UEFI for the device tree published through the
standard `gFdtTableGuid` configuration-table entry. SFB validates that FDT and
looks for exactly one `ramoops` or `qcom,ramoops` node.

That is not necessarily the final device tree used by Android. A bootloader may
publish no FDT, and Linux may dynamically allocate a reserved-memory node which
has `size` and `alloc-ranges` but no fixed `reg` address. In either case,
automatic discovery fails safely instead of guessing.

The explicit form accepts the already-resolved address and size of one
persistent-RAM zone:

```text
fastboot oem pstore console 0x<zone-address> 0x<zone-bytes>
fastboot oem pstore pmsg    0x<zone-address> 0x<zone-bytes>
```

Both numbers are mandatory hexadecimal values. The address is a physical
address, not an offset. Explicit mode still requires the complete zone to fit
inside one readable EFI memory descriptor and refuses zones larger than
16 MiB.

## Commands

```text
fastboot oem pstore
fastboot oem pstore info
fastboot oem pstore console
fastboot oem pstore pmsg
```

These forms use automatic FDT discovery. `pstore` and `pstore info` report the
region and calculated console/pmsg geometry. The record forms then inspect the
selected zone.

For an explicit zone:

```text
fastboot oem pstore console 0x<address> 0x<size>
fastboot oem pstore pmsg 0x<address> 0x<size>
```

SFB validates the persistent-RAM signature, cursor, stored length, ring bounds,
and EFI memory-map range. It emits at most the newest 48 KiB in bounded
fastboot `INFO` packets. Non-printing bytes are rendered as `\xNN`.

## Finding the live address

From rooted Android or recovery, first inspect the running kernel's view:

```sh
cat /proc/iomem | grep -i ramoops
dmesg | grep -Ei 'ramoops|persistent ram'
```

Use evidence from the running device; do not copy an address from another
phone or firmware build. Convert an inclusive `/proc/iomem` range to bytes as
`end - start + 1`.

The Infiniti device tree currently describes a dynamic `0x240000`-byte region
with no dump or ftrace zone:

```text
console: offset 0x00000, bytes 0x040000
pmsg:    offset 0x40000, bytes 0x200000
```

If Linux reports the region base as `BASE`, use `BASE` for console and
`BASE + 0x40000` for pmsg. Re-check the live range after firmware or device-tree
changes.

## Non-panic verification

Write a unique marker while Android or recovery is running:

```sh
printf 'SFB_PSTORE_TEST_123\n' > /dev/pmsg0
```

Reboot to SFB before another Linux boot can change the record, then use the live
address obtained above:

```text
fastboot oem pstore pmsg 0x<PMSG-ADDRESS> 0x200000
```

Seeing `SFB_PSTORE_TEST_123` proves the complete path: Linux selected the
address, pmsg persisted across reboot, EFI can read the range, and SFB decoded
the persistent ring correctly. No kernel panic is required.

If explicit mode reports `signature=0x00000000`, the supplied range was readable
but the persistent-RAM header did not survive the transition into SFB. Explicit
geometry cannot recover bytes that earlier firmware cleared. Infiniti currently
does this on the recovery-to-bootloader path even when the marker was written to
the live address; use recovery's `/sys/fs/pstore` for that path, or fix the
platform's ramoops reservation/preservation before expecting SFB retrieval.

## Failure meanings

- `pstore FDT discovery failed`: UEFI supplied no usable fixed ramoops region;
  use the live Linux address and explicit form.
- `pstore explicit range or record failed`: the range is outside one readable
  EFI descriptor, the size is invalid, or the bytes do not contain a valid
  persistent-RAM record.
- `no valid persistent record`: the range was readable but currently has no
  persistent-RAM signature. This is reported as a successful empty result.

Never probe guessed addresses. A wrong readable address is rejected unless it
also contains a structurally valid persistent-RAM header, but the host remains
responsible for supplying the range reported by the same device and boot stack.
