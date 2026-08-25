# ABL repo

Older ABL images that still carry the GBL vulnerability, used to downgrade the
`abl` partition on devices whose current ABL no longer has the exploit.

## Layout

```
ablrepo/
  <product>/
    abl.img       # raw ABL image with the GBL vulnerability
    abl.sha256    # sha256 of abl.img (sha256sum output format)
    abl.meta      # identity + integrity metadata (key=value, LF)
```

`<product>` is the value of `getprop ro.product.name` on the device, verbatim.

## abl.meta

`abl.meta` is a `key=value` file (LF line endings) with these keys, in order:

| key | meaning |
|-----|---------|
| `product` | Directory name, verbatim; must equal `getprop ro.product.name` on the device. |
| `model` | `getprop ro.product.model` of the device this image was taken from, or the literal `unknown`. |
| `soc` | `getprop ro.board.platform` of the device this image was taken from, or the literal `unknown`. |
| `abl_version` | Contents of `abl_version.txt` when present, else the literal `unknown`. |
| `sha256` | sha256 of `abl.img` (must equal `abl.sha256`). |
| `bytes` | Byte length of `abl.img`. |
| `same_image_as` | Optional. Comma-separated list of other `<product>` directories whose `abl.img` is byte-identical to this one. |
| `codename` | Optional, informational only. Device codename the repository evidences for this image (for example `macan`). Never gated on, because no `getprop` returns it verbatim on every build. |

`model`, `soc` and `abl_version` are only as strong as the evidence in this
repository; anything not corroborated here is recorded as `unknown`. A device
codename is not a SoC: record it in `codename`, never in `soc`, or the identity
check below will refuse the image on exactly the devices it was meant for.

**A `same_image_as` binary has NOT been verified for each listed model.** It
means one image was contributed under several product names, not that the image
was confirmed to boot on all of them. The identity checks below exist because
flashing an ABL that cannot run on the device is an unrecoverable-brick
scenario.

## Lookup order

On first install, when the current ABL lacks the GBL vulnerability,
`customize.sh` looks for an older ABL in this order:

1. **Local** — bundled inside the module ZIP at `$MODPATH/ablrepo/<product>/`
2. **Cloud** — `https://raw.githubusercontent.com/superturtlee/gbl_root_canoe/main/ablrepo/<product>/`

Both locations use the same layout. Before anything is flashed, the candidate
image must pass all of these checks:

1. `abl.meta` exists, its `product` equals `getprop ro.product.name`, and its
   `sha256`/`bytes` match the candidate image.
2. Any `model` other than `unknown` equals `getprop ro.product.model`; any
   `soc` other than `unknown` equals `getprop ro.board.platform`.
3. The candidate image is extracted and patched locally, and the GBL patch
   must succeed — an image that does not take the patch is refused.

Only then is the image flashed to the `abl` partition, and the patched loader
and TrustZone map staged alongside it are re-derived from the *flashed* ABL,
not from the one that was on flash before the downgrade.

## Adding a device

1. Obtain an older ABL for the device that still has the GBL vulnerability.
2. Name it `abl.img` and place it under `ablrepo/<product>/`.
3. Generate the checksum: `sha256sum abl.img > abl.sha256`.
4. Add `abl.meta` with the keys above; record `unknown` for anything you
   cannot corroborate. Never claim a `model`/`soc` you did not read off a
   device the image actually booted on.
5. If the image is byte-identical to another product's, add `same_image_as`
   on both sides — and understand it stays a warning sign, not an endorsement.
6. Commit locally (bundled into the module) and push to `main` (cloud).
