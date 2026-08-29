# Chainloading a third-party UEFI stack

BDS is a chainloader selector, not a boot manager. It enumerates candidate
images and starts one, and that is the whole of it: `LoadImage` followed by
`StartImage`, with the row's `options` handed over byte for byte. It does not
enumerate USB, it does not implement a boot manager for other operating systems,
and it does not carry loaders for payload formats.

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
| Linux | GRUB, or any EFI-stub kernel | starts the PE |
| Another bootloader, including a self-compiled ABL | its UEFI application form | starts the PE |
| Android | the managed `boot.efi` | starts the PE, with mode hooks |

A `canoe.cfg` row pointing at a self-compiled ABL is a legitimate entry: the
inner artefact of something like `abl2esp` is an ordinary UEFI application before
it gets wrapped for the `abl` partition.

The full analysis — reference implementations, the four candidate pathways, and
what each upstream project would change — lives in the `canoe-uefi-handoff`
side project.

## Not a managed launch

A row that is not one of the four managed Android paths is a passthrough: the
`efisp` recursion guard and the Mode 1/2 policy hooks are not armed around it.
That is correct, because the payload owns the machine afterwards and those hooks
would have nothing left to govern.
