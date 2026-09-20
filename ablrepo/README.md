# ABL test fixtures

Repository test-fixture data for the ABL extractor goldens and the image tests.
**Not** a user-facing ABL catalogue: nothing packaged into a shipped artifact
carries it, no device flow reads it, there is no remote mirror, and users supply
their own ABL images.

## Who reads it

- `submodules/ablfvextractor/tests/goldens.json` records the expected extracted
  digest and length for the 13 images under `ablrepo/<product>/`;
  `tests/goldens.rs` re-derives each one from `ablrepo/<product>/abl.img`.
- `tools/canoe-image/tests/cli.rs` uses `ablrepo/CPH2767/abl.img` as an input
  image.

Replacing an image invalidates the golden recorded for it, so a fixture image is
replaced deliberately and its golden is re-recorded in the same change.

## Layout

```text
ablrepo/
  <product>/
    abl.img       # stock ABL, kept as extractor/image-test input
    abl.sha256    # sha256sum output for abl.img
    abl.meta      # identity and integrity metadata
```

`<product>` is the exact value of `getprop ro.product.name` on the device the
image was pulled from. That is what keeps fixture names aligned with the
per-product rows the extractor records.

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
here. Use `unknown` when a value was not read from a device that booted the
image. A codename is not a SoC: record it under `codename`, never under `soc`.

`same_image_as` is fixture bookkeeping: one image is recorded under several
product names so the same bytes are not stored twice. It is not a claim that the
image boots on every listed model.

## Adding or replacing a fixture

1. Place the image at `ablrepo/<product>/abl.img`.
2. Generate `sha256sum abl.img > abl.sha256`.
3. Add `abl.meta`, using `unknown` for every uncorroborated value.
4. If it is byte-identical to another fixture, add `same_image_as` on both sides.
5. Re-record the extractor goldens for an image they cover.
6. Run `make version-check` from the repository root: the `ablrepo` data row
   verifies every entry's image against its `abl.sha256` file and against the
   `sha256=` and `bytes=` values in its `abl.meta`, so a mis-ingested fixture
   fails the gate instead of first failing a test. Then run
   `cargo test --locked --manifest-path submodules/ablfvextractor/Cargo.toml` to
   prove the goldens still resolve their fixtures.

These are stock firmware images kept as test input. Nothing here is a flash
payload, and a fixture image is never written to a device.
