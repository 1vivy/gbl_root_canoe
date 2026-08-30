# Chainloading a third-party UEFI stack

BDS is a read-only UEFI selector. It scans retained volumes for well-known EFI
loaders and BLS Type #1 entries, and starts the selected one. For a plain row
that remains `LoadImage` followed by `StartImage`, with the row's `options`
handed over byte for byte; a BLS `linux` row additionally publishes its initrd
and device tree to an EFI-stub kernel. BDS is not a general operating-system
boot manager, does not parse Android boot images or raw firmware descriptors,
and does not carry payload loaders.

So the contract for anything you want to chainload is short: **it must be a UEFI
application**.

```text
entry mu
  title Mu-Silicium (infiniti)
  image mu/place.efi
  options \efisp\mu\Mu-infiniti.bin
```

`image` is the PE. `options` is whatever that PE's own argument grammar wants —
BDS neither parses nor validates it. See the
[`canoe.cfg` contract](./canoe-cfg.md) for the grammar, and in particular why
the `options` path carries `\efisp` while `image` does not.

That example is `place.efi`, which lives in the `canoe-uefi-handoff` side
project. Its argument is a path and nothing else: the blob it enters is a Project
Mu boot shim followed by the descriptor, and the shim's header already carries
the load base and window size, so there is no hex for anyone to transcribe. An
earlier design took `<path> <base> <size>`; two of four device cycles were lost
to getting those numbers and their prefix right, which is why the surviving
design does not ask for them.

## BLS Type #1 entries

BLS provides a second declaration namespace for bootable artifacts. Each
`loader/entries/<name>.conf` file is one Type #1 row and must contain exactly
one `linux` or `efi` key. A `linux` row names an EFI-stub kernel and may name
one `initrd`, one `devicetree`, and command-line `options`; an `efi` row names
an ordinary UEFI application and uses `options` as its opaque LoadOptions.
Unknown standard BLS keys are retained for compatibility, while malformed
entries, missing images, and unsupported duplicate fields are skipped.

The boot manager stages a row and every referenced artifact with SHA-256
verification:

```bash
# A local boot-root directory:
sha256sum vmlinuz-canoe initramfs-canoe
canoe-bootmgr --boot-root /path/to/efisp bls stage \
  --name canoe-linux.conf --entry ./canoe-linux.conf \
  --artifact ./vmlinuz-canoe,vmlinuz-canoe,<KERNEL_SHA256> \
  --artifact ./initramfs-canoe,initramfs-canoe,<INITRD_SHA256>

# A direct ext4 image or exported block source:
canoe-bootmgr --source <ext4-image-or-block-device> bls stage \
  --name canoe-linux.conf --entry ./canoe-linux.conf \
  --artifact ./vmlinuz-canoe,vmlinuz-canoe,<KERNEL_SHA256> \
  --artifact ./initramfs-canoe,initramfs-canoe,<INITRD_SHA256>
```

`--artifact` is `SOURCE,DESTINATION,SHA256`; every destination must be
referenced by the parsed BLS file, and every digest must be 64 hexadecimal
characters. The operation verifies the source before and during the copy,
writes `loader/entries/<name>.conf` only after all artifacts pass, and rolls
back the whole set on failure. `--source` and `--ext4-image` select the direct
ext4 backend; `--boot-root` selects a local directory and cannot be combined
with them.

### The two path namespaces

The two declaration grammars name paths relative to different roots:

| Declaration | Path value | Persist ext4 resolution | FAT resolution |
| --- | --- | --- | --- |
| `canoe.cfg` `image` | `mu/place.efi` | `\efisp\mu\place.efi` | `\mu\place.efi` |
| `canoe.cfg` `options` | payload-owned opaque value | passed unchanged; a payload path starts at `\` | passed unchanged; a payload path starts at `\` |
| BLS `linux`/`efi`/`initrd`/`devicetree` | relative or leading-`/` path | prefixed to `\efisp\...` | volume-root `\...` |

Thus a BLS file staged in `persist/efisp/loader/entries` can say
`linux /vmlinuz-canoe`, and BDS opens `\efisp\vmlinuz-canoe`. A Canoe row
staged in the same boot root says `image mu/place.efi` without the prefix.
The path in a plain row's `options` belongs to the launched payload and must
include `\efisp` when that payload lives on persist.

### Discovery is not an unattended default

BDS appends discovered BLS rows after configured `canoe.cfg` rows. The
unattended default resolver accepts only a non-removable plain EFI row from
`canoe.cfg`; a discovered BLS `efi` or `linux` row cannot be named by
`canoe.cfg default`. Hold **Volume Up** during the startup sampling window,
choose the BLS row in the menu, and press Power. For repeatable unattended
tests, add a small wrapper UEFI application as a plain `canoe.cfg` row and
make that wrapper row the default; the wrapper can select or chain to the BLS
artifacts.


## Why BDS ships no payload loaders

It used to ship two: one that copied a raw firmware descriptor to a fixed
physical address and jumped, and one that parsed Android boot images and
assembled a kernel handoff. Both were removed, for a reason worth recording.

A firmware descriptor from a Project Mu port is linked to execute at a fixed
base. Placing it there means asking the live UEFI allocator for that exact
address, and the allocator is entitled to refuse — measured on the OnePlus 15,
it does:

```text
FdLoader: reserve 0xC6900000 (3145728 bytes) failed (Not Found)
```

The device tree carries no carveout over that range and the kernel reports it as
ordinary `System RAM`, so the refusal is the firmware's own allocator holding
pages there. Overriding the reservation and copying anyway would write over
memory the running firmware may still be using, before `ExitBootServices`, with
no diagnostic possible.

The correct place for that copy is *after* `ExitBootServices`, where no allocator
exists — which is exactly what a Project Mu boot shim does, and why upstream
ships one. That code belongs with the descriptor whose link address it hardcodes,
not in a selector that has no business knowing what a load base is.

Reference points for the same conclusion: Qualcomm's own `abl2esp` boots another
image with nothing but `LoadImage`/`StartImage` on `\EFI\BOOT\BOOTAA64.EFI`, and
GRUB's arm64 direct loader never requests a fixed base — it takes whatever the
allocator gives and aligns inside it.

## What this means in practice

| You want to boot | Ship as | BDS does |
| --- | --- | --- |
| A Project Mu / Aloha firmware descriptor | a UEFI application that places it after `ExitBootServices` | starts the PE |
| Linux | GRUB, or any EFI-stub kernel, in a plain row or BLS `linux` entry | starts the PE; BLS publishes initrd/DTB |
| Another bootloader, including a self-compiled ABL | its UEFI application form, in a plain row or BLS `efi` entry | starts the PE |
| Android | the managed `boot_a.efi`, `boot_b.efi`, or `boot_backup.efi` triplet | starts the PE, with mode hooks |

A `canoe.cfg` row pointing at a self-compiled ABL is a legitimate entry: the
inner artefact of something like `abl2esp` is an ordinary UEFI application before
it gets wrapped for the `abl` partition.

The full analysis — reference implementations, the four candidate pathways, and
what each upstream project would change — lives in the `canoe-uefi-handoff`
side project.

## Not a managed launch

A row that is not one of the current managed boot-root paths is a passthrough:
the `efisp` recursion guard and the Mode 1/2 policy hooks are not armed around
it. This includes every BLS row and every removable-media row. That is correct,
because the payload owns the machine afterwards and those hooks would have
nothing left to govern.
