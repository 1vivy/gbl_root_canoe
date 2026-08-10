# ABL repo

Older ABL images that still carry the GBL vulnerability, used to downgrade the
`abl` partition on devices whose current ABL no longer has the exploit.

## Layout

```
ablrepo/
  <product>/
    abl.img       # raw ABL image with the GBL vulnerability
    abl.sha256    # sha256 of abl.img (sha256sum output format)
```

`<product>` is the value of `getprop ro.product.name` on the device, verbatim.

## Lookup order

On first install, when the current ABL lacks the GBL vulnerability,
`customize.sh` looks for an older ABL in this order:

1. **Local** — bundled inside the module ZIP at `$MODPATH/ablrepo/<product>/`
2. **Cloud** — `https://raw.githubusercontent.com/superturtlee/gbl_root_canoe/main/ablrepo/<product>/`

Both locations use the same layout. The image is verified against `abl.sha256`
before use. After verification, the ABL is re-patched to confirm it actually
has the GBL vulnerability; only then is it flashed to the `abl` partition.

## Adding a device

1. Obtain an older ABL for the device that still has the GBL vulnerability.
2. Name it `abl.img` and place it under `ablrepo/<product>/`.
3. Generate the checksum: `sha256sum abl.img > abl.sha256`.
4. Commit locally (bundled into the module) and push to `main` (cloud).
