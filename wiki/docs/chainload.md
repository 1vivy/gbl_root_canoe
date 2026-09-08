# Chainloading EFI and Linux payloads

BDS loads ordinary EFI applications through LoadImage/StartImage. BLS Linux
entries additionally publish an initrd and device tree for an EFI-stub kernel.
Managed Mode 1/2 hooks and sidecars apply only to managed Android loaders in the
owned device boot root. Other EFI/BLS payloads are passthrough.

Place files in the mounted efisp.fat root or ordinary removable FAT media.
Configuration image paths and BLS image paths resolve from that filesystem
root. Options are passed unchanged to the payload; its paths also start there.
Do not prefix paths with the old ext4 efisp directory.

Install a complete BLS set with the [mounted-root CLI](./commands.md), using
`--artifact BOOT_ROOT_PATH=SOURCE_FILE` for every referenced image. The command
checks formats and publishes images before the entry. Application-managed
operations provide their own snapshots/readback/recovery. An example BLS file
is [pmos.conf](./examples/pmos.conf).

The default may name a discovered BLS row on the owned device boot root, such
as `bls:pmos`. A missing or unreadable default opens the menu instead of silently
launching another row. External removable media cannot supply an unattended
default for the device boot root. Ordinary selections remain one-shot; the
explicit Save as default action persists the choice.

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
inner artefact of something like `abl2esp` is an ordinary UEFI application
before it gets wrapped for the `abl` partition.

The full analysis — reference implementations, the four candidate pathways,
and what each upstream project would change — lives in the `canoe-uefi-handoff`
side project.

## Not a managed launch

A row that is not one of the current managed boot-root paths is a passthrough:
the `efisp` recursion guard and the Mode 1/2 policy hooks are not armed around
it. This includes every BLS row and every removable-media row. That is correct,
because the payload owns the machine afterwards and those hooks would have
nothing left to govern.
