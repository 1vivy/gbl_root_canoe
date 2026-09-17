# RAM-loaded source contract and autonomous Super Fastboot loop

This is a development harness. It does not change the installed-product source
contract: an installed BDS/surfacer may reopen the exact, unique GPT partition
label `efisp`. A RAM-loaded child must not substitute that installed partition
for the bytes downloaded by the host.

## RAM source descriptor

With `ENABLE_BOOT_CMD`, the existing Fastboot `boot` command accepts an Android
boot image whose kernel begins with `MZ`. `kernel_size` may cover the complete
superset: the leading PE plus an appended FAT. `LoadImage` maps only the PE, but
before loading it FastbootLib publishes `CANOE_RAM_SOURCE_TABLE` under
`CANOE_RAM_SOURCE_TABLE_GUID`.

The version 1, 64-byte record contains:

- magic `CRS1`, version and header size;
- the physical base and byte length of the complete kernel payload; and
- SHA-256 of exactly those bytes.

The producer owns the bytes for the synchronous `StartImage` lifetime and
removes and clears the record whenever `LoadImage` or `StartImage` returns. A
consumer must find the table, validate every fixed field and bound, recompute
the digest, validate its package structure, and fail closed on absence or
mismatch. It must not scan for a first MZ/FAT handle, hash its relocated mapped
image, or reopen installed `efisp` during a RAM boot.

The digest is an integrity binding to the trusted firmware producer, not a
signature by the host. Fastboot `boot` remains a development interface on an
unlocked device; it does not cryptographically authenticate an untrusted host.

## Why the existing command is extended

No second command is needed. `IsEfiInBootImg` already extracts the entire kernel
range and `BootEfiImage` already buffer-loads it. Setting `kernel_size` to the
superset length makes the existing command generic enough to carry the whole
package, while ordinary PE payloads remain valid. The descriptor only exposes
the source range already owned by that command.

## Keyless Super Fastboot policy

The existing first-run path is not a policy mechanism. If `persist/efisp.fat`
cannot be opened, `SfbBootRootObserve` reports an unavailable root and
`LinuxLoader` enters Super Fastboot directly. If the root is empty or absent,
the menu counts down through its temporary setup row. Since `canoe.cfg` and the
payloads live inside `efisp.fat`, deliberately removing that file would also
remove the policy and boot root.

The supported policy is therefore a typed resident action:

```text
entry super-fastboot
  title Super Fastboot
  action fastboot
```

`image` and `action` are mutually exclusive. The only version 1 action is
`fastboot`; unknown actions fail closed. A configured fastboot action can be a
Silent direct default or a Menu countdown target and dispatches to the same
resident loop as **Enter Super Fastboot**. The permanent built-in row remains
ineligible for unattended dispatch because it has no configured ID.

## Authorized device payload (not written by this branch)

After firmware containing this grammar is installed, the supervised session
will replace `\canoe.cfg` in the FAT16 filesystem stored at
`/persist/efisp.fat` with exactly:

```text
version 1
generation 2
menu-mode silent
key-window 500
menu-timeout 5
show-booting yes
default super-fastboot
mode 1
devinfo-repair asneeded

entry super-fastboot
  title Super Fastboot
  action fastboot

entry android-a
  title Android (slot A)
  image boot.efi
  mode 1
  role active
```

The 500 ms window preserves Volume Up as an escape to the menu. No device write
is part of this source change.

Rollback is the complete retained original
`.work/device-backup/canoe.cfg.orig`: 12 lines, 164 bytes, SHA-256
`a54fc21778ee768a34398891eadc42c2eb02d681bcf7755d309b311274379d78`.
Restore those bytes as `\canoe.cfg`. This is a semantic policy rollback, not a
byte-for-byte rollback of FAT allocation metadata, the ext4 backing file, or
its journal.
