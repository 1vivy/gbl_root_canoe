# Canoe tools working agreement

## Ownership map

- `canoe-bootmgr/`: small mounted boot-root command/library. Canonical config/BLS and prepared loader operations; no application deployment protocol, ext4 backend, USB, or GUI dependency resolution.
- `canoe-image/`: explicit image inspection, derivation, graft and vendor_boot operations, using existing native helpers.
- `canoe-provision/`: explicit container inspect/create/remove on mounted persist or offline images. No device discovery or deployment wizard.
- The sibling manager application owns its native worker, OS adapters, dependency resolution, userdata assessment, snapshots, readback, operation journals, retry/revert and guided uninstall. KSU install-time input uses that same engine.
- `canoe/`: retired by the approved overhaul. Remove its interactive wrapper and packaging rather than maintaining a second guided frontend.
- `canoe-ext4/`: unchanged libext2fs for offline persist provisioning, outside ordinary mounted FAT operations.
- `mode2-profile/`, `abl-tzmap/`, patcher and extractor: shared image producers/parsers.

## Command and application boundaries

Commands accept explicit paths and operations, validate formats/path bounds, stage file publication and report write/flush failures. They do not require installation-history evidence or GUI acknowledgement tokens for ordinary entry edits. Multi-step deployment snapshots, readback comparison and recovery are application responsibilities. Do not preserve tests that impose the superseded all-in-one CLI architecture.

Retain one implementation of config/image primitives. Application adapters resolve their own packaged dependencies and permissions. Invoke native helpers with explicit argv; never construct shell commands from user paths. Routine filesystem work uses Windows FAT/Linux vfat/Android owned loop mounts. No concurrent raw writer to mounted storage.

Preserve selected image inputs. ABL/vbmeta derivation never implicitly flashes those inputs. Separate explicit slot/partition operations, boot-root writes, and reboot. Test fixtures must never select the physical phone.

## Wire-format discipline

GM2P and TZ-map files are consumed by firmware before an OS safety net exists. Parsing and generation must agree on exact size, version, reserved zeros, ordering, digest semantics, and allowed flags. Parse untrusted images and metadata at the boundary; reject trailing, truncated, duplicate, unsorted, overflowed, or semantically ambiguous data.

Changes to a sidecar format require synchronized Rust tests, C firmware parser tests, host/device integration tests, and documentation. Never repurpose a reserved field without a version bump.

## Versioning and artifacts

`../version.mk` is authoritative. `make bump` generates `canoe/src/version.rs` and Magisk `module.prop`; do not edit those outputs independently. Keep Rust lockfiles synchronized through Cargo, never by hand.

Do not commit tool caches, Rust `target/`, pulled partitions, generated images, boot logs, mounted boot roots, or extracted ABL contents. Test fixtures must be minimal, non-secret, and explicitly tracked.

## Verification

Use the affected package's real entry point:

```sh
cargo test --locked --manifest-path tools/canoe-bootmgr/Cargo.toml
cargo test --locked --manifest-path tools/canoe/Cargo.toml
cargo test --locked --manifest-path tools/canoe-gui/Cargo.toml
cargo test --locked --manifest-path tools/mode2-profile/Cargo.toml
cargo test --locked --manifest-path tools/abl-tzmap/Cargo.toml
make -C tools/canoe-ext4 test
make version-check
```

Tests that write an executable and then exec it must not race a sibling test thread's fork: `cargo` runs tests as threads of one process, and an inherited write descriptor makes `exec` fail with ETXTBSY. Serialize those pairs (see `canoe-bootmgr/tests/fastboot.rs`) and never mutate the process `PATH` from a test — pass a search path explicitly instead. For shell changes, test the exact host/device flow with isolated temporary trees; never use a real mounted `persist` volume as a test fixture.
