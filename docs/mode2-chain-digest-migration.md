# Mode 2 verified-boot hash: manager migration

For `canoe-boot-manager`, which must adopt this change before pinning a firmware
release that contains it. Delete this note once it has. It was written without
access to the manager source, so check its workflow statements against the real
call sites.

`verifiedBootHash` (`.gm2p` bytes 88..120) now matches Android's
`ro.boot.vbmeta.digest`, which covers the root and every chained vbmeta. The
sidecar format and BDS are unchanged.

## API changes

| Before | After |
| --- | --- |
| `derive_profile(vbmeta)`, `derive(vbmeta)` | `derive_profile(vbmeta, &chained)`, `derive(vbmeta, &chained)` |
| `prepare_loader(abl, vbmeta, policy)` | `prepare_loader(abl, vbmeta, &chained, policy)` |
| `derive_to_file(vbmeta, output)` | `derive_to_file(vbmeta, chain_source, output)` with `Option<ChainSource>` |
| `inspection.profile.system_version` and other profile fields | `inspection.system_version`, `system_spl`, `rot_digest`, `pubkey_digest` |
| `inspection.profile.vbh` | Removed: one vbmeta cannot establish this digest |
| `BuildArgs { .. }` literal | Add `chain_dir: None` or `Some(dir)`, and `chain_suffix: String` |

`chained` is a `ChainedImages`, a `BTreeMap<String, Vec<u8>>` keyed by the
**unsuffixed** partition name. Each value is the complete image or just its vbmeta
bytes. It must hold exactly the partitions the root chains.

Do not satisfy the compiler with an empty map, and do not replace a chain error
with a root-only fallback. Treat every new `DeriveError` variant and
`DeriveFileError::Chain` as a refusal: name the partition and leave any existing
sidecar in place.

## Collecting the chained images

Get the list from `chain_digest_partitions(vbmeta)`, which also rejects unsafe and
duplicate names from the untrusted root. Do not use `inspection.chain_partitions`:
it holds graft targets and leaves out `vbmeta_*`.

**Android native worker.** With the `native` feature, this follows the by-name
symlinks and reads each footer rather than the whole partition:

```rust
let source = mode2_profile::ChainSource {
    dir: Path::new("/dev/block/by-name"),
    suffix: "_a",
};
let chained = mode2_profile::load_chained_images(&vbmeta, source)?;
```

**Hosted WASM.** `read_chained_vbmeta(partition, &mut reader)` works with any
synchronous `Read + Seek`. If USB reads are async, do the same ranged reads:

1. If the name starts with `vbmeta`, read `min(partition size, 64 KiB)` from offset 0
   and stop. These partitions never use a footer.
2. Otherwise read the last 64 bytes into a `[u8; 64]` and call
   `footer::Footer::parse_tail(&tail, partition_size)`. An error is a refusal.
3. On `Some(footer)`, read `footer.vbmeta_size` bytes from `footer.vbmeta_offset`,
   refusing a size above 64 KiB.
4. On `None`, read `min(partition size, 64 KiB)` from offset 0.

## Choosing the right bytes

The hash must describe what ABL will read when this loader boots:

- **Use the slot the loader boots**, not the slot Android is running.
- **Take the root and its chain from one set.** A new root hashed with the old slot's
  chain matches neither firmware.
- **Derive after flashing.** If a workflow writes chained partitions, derive from the
  final bytes. This makes step order matter in OTA and root flows.

## When to regenerate

The digest covers vbmeta structures, not payloads. A patched `boot` with its stock
vbmeta grafted back keeps the digest; a `boot` carrying a different vbmeta changes
it. `init_boot` and `vendor_boot` are hash descriptors in the root on current
firmware, so flashing them does not change it.

Sidecars derived before this change hold the wrong hash on chained firmware until
regenerated, and BDS cannot detect that. A possible check: when Android is running
from the slot a Mode 2 entry boots, compare the entry's `.gm2p` bytes 88..120 with
`getprop ro.boot.vbmeta.digest` and offer to regenerate on a mismatch.

## Acceptance

1. **Test vector.** Use `tools/mode2-profile/tests/fixtures/vbmeta-infiniti-IN-16.0.7.201.img`
   with each of its five chained partitions set to the same 256 bytes: `AVB0`
   followed by 252 zero bytes.
   - `vbh`: `50b529df1791cf70aced19d89faa3f2f22a7ae3f354a74d48d2a8da14623c35b`
   - SHA-256 of the whole `.gm2p`: `351a027ffdc5acd0c041a7a8e30a7342f242f3d176155948afac57761b9578ff`

   A manager test pinning the old `.gm2p` digest `71f72bf5…` encodes the bug.
2. **Against `avbtool`.** With the images in one directory as `vbmeta.img`, `boot.img`
   and so on, compare with `avbtool calculate_vbmeta_digest --image vbmeta.img`.
3. **On a device.** After regenerating and booting the Mode 2 entry, the
   `verifiedBootHash` in a key attestation should equal `ro.boot.vbmeta.digest`.
