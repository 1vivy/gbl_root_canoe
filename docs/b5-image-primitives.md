# b5 image-library qualification

The browser and Android coordinator can reuse the image algorithms without
pulling their native CLI, filesystem adapters or process runners into WASM.
Dependencies should set `default-features = false`. Default features still build
the existing native commands. These libraries transform or inspect supplied bytes;
they do not own device writes, source selection, review or installed-state claims.

| Manifest / crate | Portable API | Evidence | Remaining boundary |
| --- | --- | --- | --- |
| `tools/mode2-profile/Cargo.toml` / `mode2_profile` | `inspect_vbmeta`, `inspect_vbmeta_header_evidence`, `check_vbmeta`, `derive_profile`, `Profile::{to_bytes,decode}`, `footer::Footer::parse` | Native suite and WASM target check pass | Caller must attribute the previous/current signing evidence and apply the data-compatibility policy. |
| `tools/canoe-image/Cargo.toml` / `canoe_image` | `graft::extract_bytes(&[u8]) -> Result<&[u8], GraftError>`; `graft::graft_bytes(&[u8], Vec<u8>) -> Result<Vec<u8>, GraftError>` | Native suite, WASM target check and exact pre-extraction CLI output comparison pass | Input is a partition-sized candidate. Grafting is not proof the resulting chain will boot; inspect/check it separately. |
| `tools/canoe-image/Cargo.toml` / `canoe_image` | `vendorboot::patch_bytes(Vec<u8>) -> Result<(Vec<u8>, bool), VendorBootError>` | Native and actual WASM CPIO/gzip/LZ4 outputs match pre-extraction CLI goldens | Output remains a local prepared image; boot-chain and device compatibility checks belong to the coordinator. |
| `tools/abl-tzmap/Cargo.toml` / `abl_tzmap` | `derive_bytes(&[u8], bool) -> Result<[u8; TZMAP_SIZE], DeriveFileError>`; `scan::scan`; `manifest::TzMap::decode` | Native suite and WASM target check pass | Input is already-extracted ABL PE. This is TZ-map derivation, not a replacement for the vulnerability check or patcher. |
| `tools/canoe-provision/Cargo.toml` / `canoe_provision` | `volume::initialize(&mut impl Write)`; `volume::inspect(&mut (impl Read + Seek))`; `volume::require_capacity` | Native suite and WASM target check pass | Produces/inspects the canonical 32 MiB FAT bytes. Does not insert a file into ext4 or prove BDS extent eligibility. |

The graft API takes ownership of the candidate buffer and transforms it in place.
Extraction borrows the donor range. It does not add a second complete candidate
clone. The native CLI uses these same primitives and retains atomic publication
and output verification. The synthetic pre-extraction golden output is SHA256
`7b6a80cf19e3343d403ea72a3e3112f116acf3e003edf8cea60dd008b16debb0`;
it contains the committed signed donor fixture and a 64 KiB candidate. Tests also
cover idempotent regrafting, malformed footer bounds and occupied trailing space.

The FAT build script invokes `mkfs.fat` on the build machine to embed its compact
canonical prefix. No subprocess or OS filesystem is required at browser runtime.
Historical local ext4-helper integration tests now require the explicit
`legacy-ext4-tests` feature; the retired helper is not a dependency of default b5
CI or packages.

## Not yet exposed as a browser image pipeline

- Full ABL extraction uses `extractfv` and its LZMA decoder; vulnerability
  inspection/patching calls the native `patch_abl` helper. These process boundaries
  must become byte APIs or compiled modules before browser deployment is enabled.
- Loader construction still coordinates those helpers and filesystem publication.
  Its pure GM2P and TZ-map parts are available above, but the complete pipeline is
  not browser qualified.
The vendor_boot byte API shares the unchanged header/CPIO/gzip/legacy-LZ4
algorithm with the CLI. Its existing LZ4 dependency builds and links for WASM;
there is no need to replace it based on its implementation language. Three
synthetic partition images and the pre-extraction native output SHA256 values are
committed under `tools/canoe-image/tests/fixtures/`. Native tests check those exact
outputs, retained DTB/footer bytes, repeat preparation and invalid input refusal.
The actual WASM module executes all three encodings with zero runtime imports and
matches the same outputs:

```sh
cargo build --locked --manifest-path tools/canoe-image/qualification/vendorboot/Cargo.toml --target wasm32-unknown-unknown --release
node tools/canoe-image/qualification/vendorboot/run.mjs
```

This validates the byte algorithms in a JavaScript WebAssembly engine. Browser
worker integration, memory limits for real partition sizes and device-specific
boot compatibility still need app/hardware qualification.

These results establish native regression and WASM compilation, not browser
runtime equivalence or physical boot compatibility. The app must exercise its
actual WASM wrappers against the same fixtures before presenting image preparation
as available. Physical flashing remains outside this source qualification.
