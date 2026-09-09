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

## Complete loader and boot-configuration byte APIs

`canoe_image::loader::{extract_abl, inspect_abl, prepare_loader}` now accept
firmware ABL bytes directly. Extraction uses the bounded `abl_extract` library in
`submodules/ablfvextractor`; LZMA is supplied by `lzma-rs`. The existing patcher C
algorithm is compiled as `abl_patch` for both native and WASM, with logging
removed from the library build and typed patch flags returned to callers. There
are no runtime C helpers, filesystem calls, subprocesses or imported WASM host
functions in this pipeline. The standalone C extractor remains a reference and
an investigator for its additional BMP/all-PE modes.

`prepare_loader(abl, vbmeta, TzMapPolicy)` returns the modified PE loader, its
120-byte GM2P, its 256-byte TZ map, and source/patch inspection. The original ABL
is borrowed and never becomes an implicit flash payload. The TZ map binds the
unmodified extracted ABL digest. `RecordedEvidence` requires a committed evidence
table for that exact source; explicit `ProtocolFallback` retains the existing
native CLI policy and reports `tzmap_evidence: null` when appropriate. This is
not a new sidecar format. Vulnerable boot-path detection is distinct from managed
loader preparation: firmware without the vulnerable `efisp` path can still
produce a valid managed loader. Container signatures are not verified by this
structural inspection.

Native `canoe-image build` and its probe call these same libraries. Existing
file validation, staging and publication remain native concerns; `--tools` is
accepted for interface compatibility but is no longer needed by loader building.
A saved patch report is structured JSON instead of captured helper stdout.

`canoe_bootmgr` with `default-features = false` exposes the canonical config/BLS
parse, render and edit models plus boot-volume path validation, without mounted
I/O. All document types support JSON roundtrips. Rendering refuses additional
boot directives hidden in option strings or unknown-key records.

Reference output hashes are committed in
`submodules/ablfvextractor/tests/goldens.json`: all 13 bundled ABL images, the
legacy extractor fixture, and four optional existing stock/reference images from
the sibling `gbl-chainload`. The optional inputs are not build dependencies and
are explicitly skipped when absent. Hashes came from the pre-refactor native
`extractfv`, `patch_abl`, `mode2_profile`, and `abl_tzmap` commands. Both native and
actual WASM execution matched all 18 cases on this machine. The four external
cases include firmware without the vulnerable boot path; they still match their
managed-loader outputs. Peak WASM linear memory for this corpus was 5,701,632
bytes. This measured corpus result is not a guarantee for maximum-sized inputs.

```sh
cargo test --locked --manifest-path submodules/ablfvextractor/Cargo.toml
cargo test --locked --manifest-path submodules/patcher/Cargo.toml
cargo test --locked --manifest-path tools/canoe-bootmgr/Cargo.toml
cargo test --locked --manifest-path tools/canoe-image/Cargo.toml
cargo build --locked --manifest-path tools/canoe-image/qualification/loader/Cargo.toml --target wasm32-unknown-unknown --release
node tools/canoe-image/qualification/loader/run.mjs
```

The WASM run asserts zero imports, extracted and patched loader hashes, both
sidecars, vulnerable-path classification, and config/BLS JSON/wire roundtrips.
This qualifies source transformations, not device boot compatibility or a complete
deployment. The app remains responsible for source attribution, partition sizes,
slot-specific configuration, review, writes and readback.

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

These results establish native regression and WASM runtime equivalence for the
listed fixtures, not physical boot compatibility. The app must exercise its
actual WASM wrappers against the same fixtures before presenting image preparation
as available. Physical flashing remains outside this source qualification.
