# Vulnerable ABL repository

This repository contains older stock ABL images that still carry the GBL
vulnerability. They are candidates for the device's `abl` partition when the
installed ABL no longer loads `efisp`.

## Layout

```text
ablrepo/
  <product>/
    abl.img       # raw stock ABL with the GBL vulnerability
    abl.sha256    # sha256sum output for abl.img
    abl.meta      # identity and integrity metadata
```

`<product>` is the exact value of `getprop ro.product.name` on the device.

## `abl.meta`

`abl.meta` is a UTF-8 `key=value` file with LF line endings and these keys in
order:

| Key | Meaning |
| --- | --- |
| `product` | Directory name; must equal the device product property |
| `model` | Device model from the source device, or `unknown` |
| `soc` | Board platform from the source device, or `unknown` |
| `abl_version` | Contents of `abl_version.txt`, or `unknown` |
| `sha256` | SHA-256 of `abl.img`; must equal `abl.sha256` |
| `bytes` | Byte length of `abl.img` |
| `same_image_as` | Optional comma-separated byte-identical product directories |
| `codename` | Optional informational device codename |

`model`, `soc`, and `abl_version` are only as strong as the evidence recorded
in this repository. Use `unknown` when a value was not read from a device that
booted the image. A codename is not a SoC: record it under `codename`, never
under `soc`.

`same_image_as` means only that one image was contributed under multiple product
names. It does not prove that the image boots on every listed model. An ABL
identity mismatch can be unrecoverable, so the checks below are mandatory.

## Lookup and validation

When the current ABL lacks GBL, the device installer looks in this order:

1. local module data at `$MODPATH/ablrepo/<product>/`;
2. the repository mirror at
   `https://raw.githubusercontent.com/superturtlee/gbl_root_canoe/main/ablrepo/<product>/`.

Before a candidate is written to `abl`, all checks must pass:

1. `abl.meta` exists, `product` matches `getprop ro.product.name`, and the
   recorded SHA-256 and byte count match the image;
2. every non-`unknown` `model` and `soc` matches the device properties;
3. extracting and patching the candidate succeeds, including the GBL patch.

Only after validation is the candidate written to `abl`. The patched loader and
TrustZone map are then derived from the ABL that was selected for the device.

## Adding an image

1. Obtain an older stock ABL that still has GBL for the device.
2. Place it as `ablrepo/<product>/abl.img`.
3. Generate `sha256sum abl.img > abl.sha256`.
4. Add `abl.meta`, using `unknown` for every uncorroborated value.
5. If it is byte-identical to another product image, add `same_image_as` on
   both sides and retain the warning that this is not a boot endorsement.
6. Commit the repository entry and publish the cloud mirror when appropriate.
