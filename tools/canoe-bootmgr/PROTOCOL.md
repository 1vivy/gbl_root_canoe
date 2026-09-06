# canoe-bootmgr wire protocol

**Protocol version: 1.** `canoe-bootmgr` is the only writer of a Canoe boot root and the only owner of boot policy and derivation. Clients send requests; they must not derive, mutate, or write boot-root state themselves.

The version-1 catalogue contains **40 verbs**.

## Compatibility rule

This protocol is **additive only** within a protocol version: additions may be new verbs or new **optional** request/response fields. Existing verbs, field names, requiredness, meanings, and response shapes must not change. Any other change requires a protocol-version bump. Clients must ignore response fields they do not understand, and the current server ignores unknown request fields so a newer client can safely send an optional field to an older compatible server.

Use `protocol.version` before relying on a capability. `app_version` identifies the binary release; `protocol_version` identifies this contract. `capabilities` is optional: an absent field means the server does not support any capability introduced after its release.

## Envelope, limits, and errors

Every request is one UTF-8 JSON object. Its required discriminator is `verb` (a string). JSON object fields not recognized by the selected verb are ignored. A request is limited to **64 KiB (65,536 bytes)** of decoded JSON; JSONL line terminators are not part of the request object.

A successful response is one UTF-8 JSON object, followed by `\n`:

```json
{"operation":"protocol.version","ok":true,"app_version":"0.1.0","protocol_version":1,"capabilities":["reviewed-identity","tools.inventory","mode-userdata-assessment-v1","image-zero-v1","whole-partition-write-v1"]}
```

All successful envelopes contain `ok: true` and `operation` (the response operation name). Successful serialized responses, including their terminating newline, are limited to **1,000,000 bytes (1 MB)**. Exceeding that limit returns `response-too-large` instead of the operation response.

A failed JSON response is one object, followed by `\n`:

```json
{"ok":false,"error":{"code":"request","message":"request JSON: ..."}}
```

`message` is diagnostic text, not a stable parsing surface. The current stable error codes are:

| Code | Meaning |
| --- | --- |
| `usage` | Command-line arguments are invalid or incompatible. |
| `request` | The request is too large, malformed JSON, or malformed base64url. |
| `input` | The JSONL input stream could not be read (including invalid UTF-8). |
| `operation` | The selected operation refused or failed. |
| `boot-root-missing` | The selected local boot root does not exist; no operation was attempted. |
| `ext4-missing` | The `canoe-ext4` helper exited with status 7, whose contract means the requested file or directory is missing or empty. This one code intentionally covers both cases. |
| `helper-unavailable` | A required build helper could not be resolved or is not executable. |
| `helper-failed` | A required helper could not start or exited with a status other than 7; these failures are intentionally one code because the source does not distinguish them safely. |
| `timeout` | A fastboot command or helper exceeded its deadline; the process is stopped, but descendants holding output pipes may be detached. |
| `no-response` | Fastboot identity reached its shared deadline or completed successful probes without a usable device reply. Local process/read errors and invalid UTF-8 are `operation` failures instead. |
| `fastboot-unavailable` | No bundled or PATH `fastboot` executable exists, so device identification cannot run. |
| `permission-denied` | The selected local path or raw block node could not be opened because permission was denied. |
| `device-busy` | Another process owns the device lease; the request waited up to the bounded lease deadline and was refused. |
| `export-active` | A live mass-storage export owns the link, so fastboot cannot run until it is ended. |
| `export-required` | The operation needs a live mass-storage export before raw-node access can proceed. |
| `vbmeta-duplicate-property` | `vbmeta.inspect` refused conflicting values for a repeated named build property; identical repeats are accepted, and clients must not use a partial result on conflict. |
| `vbmeta-worker-unavailable` | The `mode2_profile` worker could not be resolved or is not executable. |
| `vbmeta-worker-spawn` | The `mode2_profile` worker could not be started. |
| `vbmeta-worker-timeout` | The `mode2_profile` worker did not finish within 30 seconds. |
| `vbmeta-worker-failed` | The `mode2_profile` worker emitted a success envelope but exited unsuccessfully; no parsed result is accepted. |
| `vbmeta-read`, `vbmeta-too-small`, `vbmeta-bad-magic`, `vbmeta-bad-footer`, `vbmeta-range-invalid`, `vbmeta-release-string-invalid`, `vbmeta-unsigned`, `vbmeta-header-malformed`, `vbmeta-public-key-missing`, `vbmeta-public-key-invalid`, `vbmeta-descriptors-invalid`, `vbmeta-descriptor-malformed`, `vbmeta-property-malformed`, `vbmeta-chain-malformed`, `vbmeta-property-utf8`, `vbmeta-partition-name-utf8`, `vbmeta-duplicate-property`, `vbmeta-os-version-missing`, `vbmeta-security-patch-missing`, `vbmeta-os-version-malformed`, `vbmeta-security-patch-malformed` | `mode2_profile` rejected the image; the code identifies the specific AVB inspection failure. |
| `vbmeta-no-footer` | `vbmeta.extract` or `vbmeta.check` received an image without an `AVBf` footer. |
| `vbmeta-chain-partition-missing` | `vbmeta.check` found no chain descriptor for the requested partition in the main vbmeta. |
| `partition-name-invalid` | `block.read` and `block.write` rejected a partition name outside ASCII `[A-Za-z0-9_]`, 1..=36 bytes. |
| `partition-missing` | `block.read` or `block.write` could not find the resolved `/dev/block/by-name` node. |
| `not-a-block-device` | `block.read` or `block.write` resolved a regular file instead of a device node. |
| `image-too-large` | `block.write` or `fastboot.flash` received an empty image; `block.write` also uses it when the image exceeds its target. No target bytes were written. |
| `block-not-writable` | `block.write` could not make the resolved target writable. |
| `snapshot-failed` | `block.write` could not save and flush the target snapshot; no target bytes were written. |
| `readback-mismatch` | `block.write` readback differed from the source and restored the snapshot successfully. |
| `rollback-failed` | `block.write` readback differed and restoring the snapshot failed; the response names the snapshot artifact. |
| `digest-range` | `image.digest` was asked to hash more bytes than the image contains; no digest was returned. |
| `digest-mismatch` | `abl.verify` image SHA-256 differed from the supplied expected digest; no probe was run. |
| `reboot-unavailable` | `system.reboot` could not find an executable `reboot` in `PATH`. |
| `ablrepo-unavailable` | `abl.lookup` could not obtain the repository metadata, digest, or image. |
| `ablrepo-digest` | `abl.lookup` image SHA-256 differed from `abl.sha256`. |
| `ablrepo-metadata` | `abl.lookup` metadata was missing, malformed, or disagreed with the image. |
| `vendorboot-header` | `vendorboot.patch` refused an image whose vendor_boot header is invalid; no output was written. |
| `vendorboot-cmdline-full` | `vendorboot.patch` refused an image whose cmdline field has no room for Canoe's blacklist; no output was written. |
| `vendorboot-ramdisk` | `vendorboot.patch` refused unsupported, malformed, oversized or ambiguous ramdisk content, or found no guard module metadata; no output was written. |
| `vendorboot-output` | `vendorboot.patch` received an invalid output path; no patched image was returned. |
| `unsupported-platform` | `block.read`, `block.write`, or `system.reboot` is unavailable on this platform. |
| `tools-source-missing` | `tools.update` source does not exist; no boot-root write was attempted. |
| `tools-source-not-directory` | `tools.update` source is not a directory; no boot-root write was attempted. |
| `tools-source-empty` | `tools.update` source contains no regular files; no boot-root write was attempted. |
| `tools-source-name` | `tools.update` source contains a file whose name cannot be represented in the wire receipt. |
| `tools-snapshot` | `tools.update` could not snapshot an existing destination; no boot-root write was attempted. |
| `tools-write` | `tools.update` failed while copying a staged file; the pre-update snapshot was restored. |
| `tools-rollback` | `tools.update` failed and could not restore the pre-update snapshot. |
| `tools-inventory-required` | `install` or `ota-apply` found staged tools without a reviewed `staged_tools` inventory; no boot-root write was attempted. |
| `tools-inventory-mismatch` | A supplied staged-tools inventory had a missing, duplicate, added, removed, or digest-mismatched tool; no boot-root write was attempted. |
| `ota-active-slot-unknown` | `ota-apply` received an explicit target without authoritative active-slot metadata; no boot-root write was attempted. |
| `mode-plan-invalid` | `mode.plan` received a mode outside `0..=2`; no boot-root lookup or write was attempted. |
| `mode-precondition-unsatisfied` | `entry.mode`, `install`, or `ota-apply` was asked to apply a transition whose planned preconditions are missing, unknown, or require a post-action. |
| `mode-evidence-missing` | `install` or `ota-apply` could not derive an effective source/target mode, or a Mode 1 artifact had no target vbmeta provenance; no mutation was attempted. |
| `graft-required` | A Mode 1 install or OTA target carries tree-built/ungrafted vbmeta evidence; a verified graft is required before mutation. |
| `response-too-large` | A successful response exceeded 1 MB. |
| `output` | The response could not be encoded or written. |

JSON success exits with status 0. JSON request, input, operation, and response failures exit 1; CLI usage failures exit 2.

## Transports

### JSONL session: `--json`

With `--json` and no subcommand, stdin is a JSON Lines session: write one request object plus a newline, and read one response line for each non-empty input line. The session continues after a per-request error, but the process exits 1 if any line failed. A stream-read failure emits one `input` error line and ends the session.

### One shot: `--request-b64 TOKEN`

`--request-b64` accepts exactly one unpadded base64url (`A-Z`, `a-z`, `0-9`, `-`, `_`) token containing the UTF-8 request JSON. It emits exactly one JSON response line to stdout. The token has an implementation guard of 128 KiB before decoding; the decoded request remains subject to the 64 KiB request limit. It cannot be combined with a CLI subcommand.

The KernelSU WebUI in the separate `canoe-boot-manager` application uses this transport from `src/lib/transport/device.ts` and only requires the stable `ok` boolean plus the error envelope on failure. Version 1 remains compatible with that client.

## Boot-root addressing

The request envelope normally inherits the process-level root. Process-level options choose it:

- `--boot-root PATH` selects a mounted Canoe persist/efisp directory; absent means the current directory.
- `--source PATH` selects a direct ext4 persist image or raw block source, whose fixed boot root is `/efisp`.
- `--ext4-image PATH` is an alias for `--source`.

`--source` and `--ext4-image` conflict. Boot-root operations resolve this addressing once in
`canoe-bootmgr`; clients never address raw boot-root files directly. Every direct ext4 source is
treated as a persist volume: reads use `/efisp` without creating it, while writes create `/efisp`
before adding boot-root content. A missing `/efisp` never permits reading or writing the filesystem
root; pre-existing root files are neither adopted nor moved. For `install`, `ota-apply`, and
`tools.update`, the optional request field `boot_root_source:path` selects an ext4 image or
exported persist node when no global `--source` was supplied. When global `--source` is present,
it is authoritative: a request-local `boot_root_source` may repeat the same path but a different
path is rejected. `protocol.version`, `build`, and fastboot requests do not need a boot root.
`source.detect` discovers candidate roots rather than reading the selected one. The
`tools.inventory` request does not use a boot root; it inventories the regular files directly under
its required `source` directory.

## Types used below

`string` is a JSON string, `bool` a JSON boolean, `u8`/`u32`/`u64` non-negative JSON integers, and `path` a string serialized from a platform path. `T?` means the field is optional in a request or may be `null` in a response. `[]` means an array.

Shared response records:

- `raw_line`: `{key:string,value:string}`.
- `entry`: `{id:string,title:string,image:string,options:string?,mode:u8,role:"active"|"inactive"|"backup"|"other",unknown:raw_line[]}`.
- `config`: `{entries:entry[],generation:u32,menu_mode:"silent"|"menu",key_window_ms:u32,menu_timeout_s:u32,default:string?,mode:u8,devinfo_repair:"asneeded"|"never",unknown:raw_line[]}`.
- `bls_entry`: `{title:string?,kind:"linux"|"efi",image:string,initrd:string?,devicetree:string?,options:string,unknown:raw_line[],rejected_lines:usize}`.
- `bls_file`: `{name:string,entry:bls_entry}`.
- `file_identity`: `{path:path,bytes:u64,sha256:string}`. Paths identify the files whose bytes and SHA-256 were reviewed.
- `mode_userdata`: `{requirement:"must"|"may"|"not-required",reasons:{rule:string,reason:string}[]}`. `must` is a mandatory userdata-format precondition; `may` is warning evidence and never creates a `P-FORMAT` blocker.
- `mode_header_evidence`: `{algorithm_type:u32,rollback_index:u64,public_key_sha256:string?,build_properties:{system_os_version:string?,system_security_patch:string?,vendor_security_patch:string?,boot_security_patch:string?}}`. A signer-key digest identifies a key only; it does not prove that the signer is OEM.
- `install_receipt`: `{active_slot:"a"|"b",installed:("a"|"b")[],generation:u32,signer_changed:bool,backup_present:bool,staged:path,loader_bytes:u64,loader_sha256:string,gm2p_bytes:u64,gm2p_sha256:string,tzmap_bytes:u64,tzmap_sha256:string,tools:file_identity[],mode_request:{id:string?,from_mode:0|1|2|null,target_mode:0|1|2|null,current_vbmeta:path?,target_vbmeta:path?,target_image:path?,tools:path?,prior_canoe:bool,acknowledge:string[]}?,acknowledged:string[],warnings:string[]}`. The `mode_request` field is always serialized; it is `null` when the request omitted `mode`, and is the identity object when a mode was requested. The policy arrays are the preconditions acknowledged and warning codes emitted by the apply-time mode plan.

Artifact replacements are assessed even when the requested mode is unchanged.
Mutation warning arrays include unsatisfied `P-PROFILE` and distinct `R2`, `R3`,
or `R4` reason codes when the userdata assessment is `may`. These advisory codes
do not require a `P-FORMAT` acknowledgement; Mode 1 target-evidence refusals remain
mandatory.

| Verb | Request fields | Success response fields |
| --- | --- | --- |
| `protocol.version` | `verb` only | `operation:"protocol.version"`, `app_version:string`, `protocol_version:u32`, `capabilities:string[]?`. New servers advertise `reviewed-identity`, `tools.inventory`, `mode-userdata-assessment-v1`, `image-zero-v1`, and `whole-partition-write-v1`; an absent field means none of those advertised capabilities are supported. Clients must require `mode-userdata-assessment-v1` before relying on a mode plan to authorize an install or OTA write, must not issue `image.zero` without `image-zero-v1`, and must not issue a write carrying `expected_partition_bytes` without `whole-partition-write-v1`. |
| `build` | `abl:path` required; `vbmeta:path?`, `staged:path?`, `tools:path?`, `efisp_tools:path?`, `keep_unpatched:path?`, `patch_log:path?`, `probe:bool?`. | Full build: `operation:"build"`, `kind:"build"`, `receipt:{staged:path,loader_bytes:u64,gm2p_bytes:u64,tzmap_bytes:u64,tools_staged:usize,gbl_patched:bool,loader_sha256:string,gm2p_sha256:string,tzmap_sha256:string,unpatched_sha256:string}`. Probe build: `operation:"build.probe"`, `kind:"build.probe"`, `receipt:{gbl_patched:bool,unpatched_sha256:string}`. |
| `abl.verify` | `image:path` required; `expected_sha256:string?`. If supplied, verification returns `digest-mismatch` before probing on a mismatch. | `operation:"abl.verify"`, `sha256:string`, `gbl_patched:bool`. The vulnerability result comes from the existing `build --probe` path. |
| `image.digest` | `image:path`, `bytes:u64?`. Without `bytes`, hash the complete image; with it, hash exactly that prefix and refuse a range beyond the image length. | `operation:"image.digest"`, `path:string`, `sha256:string`, `bytes:u64`. |
| `image.zero` | `output:path`, `bytes:u64` required. Creates an exactly `bytes`-long all-zero regular image and returns its identity; `bytes` must be nonzero. | `operation:"image.zero"`, `output:string`, `bytes:u64`, `sha256:string`. |
| `abl.lookup` | `product:string`, `output:path`, `local_repo:path?`. A local repository resolves `<local_repo>/<product>/abl.{img,sha256,meta}`. Without it, files are fetched from `CANOE_ABLREPO_URL` or the default repository URL. | `operation:"abl.lookup"`, `product:string`, `output:string`, `sha256:string`, `bytes:u64`, `source:"local"|"remote"`. |
| `block.write` | `partition:string`, `image:path`, `snapshot:path` required; `slot:"a"|"b"?`, `expected_bytes:u64?`, `expected_partition_bytes:u64?`, `expected_sha256:string?`, `expected_snapshot_bytes:u64?`, `expected_snapshot_sha256:string?`. The partition must match ASCII `[A-Za-z0-9_]` and be 1..=36 bytes. The server resolves `/dev/block/by-name/<partition><suffix>`, snapshots the full target, writes without truncation, verifies readback, and restores on mismatch. When expected identities are supplied, the corresponding source image and snapshot are verified before mutation. `expected_partition_bytes` requires both the resolved target size and source image size to equal that value before writing; clients must capability-gate this whole-partition safety contract with `whole-partition-write-v1`. | `operation:"block.write"`, `partition:string`, `node:string`, `bytes_written:u64`, `sha256:string`, `snapshot:string`, `snapshot_bytes:u64`, `snapshot_sha256:string`, `verified:bool`. |
| `block.read` | `partition:string`, `output:path`, `slot:"a"|"b"?`. The server resolves `/dev/block/by-name/<partition><suffix>`, reads its complete target size into `output`, and hashes the result. | `operation:"block.read"`, `partition:string`, `node:string`, `output:string`, `bytes:u64`, `sha256:string`. |
| `tools.update` | `source:path` required (the tools directory); `boot_root_source:path?` optionally selects the ext4 image or exported persist node receiving the update; `inventory:file_identity[]?` optionally supplies the reviewed source inventory, which is verified before any boot-root write. The source must be a directory containing at least one regular file. Each direct child file is copied to `tools/<name>` through the boot-root transaction; destinations are snapshotted and restored if any copy fails. | `operation:"tools.update"`, `files:string[]` (sorted names written to the boot root), `inventory:file_identity[]` (the reviewed source files). |
| `tools.inventory` | `source:path` required (a directory containing at least one regular file). | `operation:"tools.inventory"`, `inventory:file_identity[]` (the direct child files, sorted by path). |
| `config.show` | `verb` only | `operation:"config.show"`, `config:config`. |
| `config.set-policy` | `menu_mode:"silent"|"menu"?`, `key_window_ms:u32?`, `menu_timeout_s:u32?`. | `operation:"config.policy"`, `kind:"config.policy"`, `config:config`, `generation:u32`, `mark:string`. |
| `entry.list` | `verb` only | `operation:"entry.list"`, `generation:u32`, `entries:entry[]`. |
| `entry.set` | `id:string`, `title:string`, `image:string`, `role:"active"|"inactive"|"backup"|"other"` required; `options:string?`, `mode:u8?`, `global_mode:u8?`, `devinfo_repair:"asneeded"|"never"?`, `default:bool?`. | `operation:"entry.set"`, `generation:u32`, `entry:entry`, `mark:string`. |
| `entry.remove` | `id:string` required. | `operation:"entry.remove"`, `generation:u32`, `mark:string`. |
| `entry.mode` | `id:string`, `mode:u8` required; `acknowledge:string[]?` acknowledges operator-action preconditions (`P-GRAFT`, `P-FORMAT`); `current_vbmeta:path?`, `target_vbmeta:path?`, `target_image:path?` optionally provide evidence; when `target_image` is present, its embedded vbmeta takes precedence over `target_vbmeta`; the CLI-only `tools:path?` selects the worker. | `operation:"entry.mode"`, `generation:u32`, `acknowledged:string[]`, `warnings:string[]`, `mark:string`. `P-PROFILE` is reported as a warning and never blocks; operator-action preconditions must be acknowledged. |
| `mode.plan` | `id:string?`, `target_mode:0|1|2` required; `from_mode:0|1|2|null?` supplies source-mode evidence when `id` is absent, and omitted or `null` evidence remains unknown; `prior_canoe:bool?` defaults to `false` and records whether the original state is recognized Canoe content. A persisted entry selected by `id` is recognized independently of the flag. `current_vbmeta:path?`, `target_vbmeta:path?`, `target_image:path?` optionally provide evidence; when `target_image` is present, its embedded vbmeta takes precedence over `target_vbmeta` and must carry an `AVBf` footer. | `operation:"mode.plan"`, `id:string?`, `plan:{from_mode:0|1|2|null,target_mode:0|1|2,preconditions:{code:"P-GRAFT"|"P-PROFILE"|"P-FORMAT",rule:string,blocking:bool,satisfied:bool?,reason:string}[],post_actions:{action:string,reason:string}[],outcome:{status:"ready"|"cannot-predict",reason:string?},refusal:{code:string,reason:string}?,vbmeta:{current:mode_header_evidence?,target:mode_header_evidence?,relationship:"lower"|"same-or-higher"?},userdata:mode_userdata}`. Known transitions crossing mode `0` are mandatory `R1`; the lower four-property compatibility boundary between modes 1 and 2 is `R3` `may` evidence with the unavailable KeyMint floor named; changed signing/provenance is `R4` `may` evidence naming the common soft-brick risk. Missing source evidence remains `may`. Non-Canoe original state is `may` evidence, suppressed only by an original BDS payload match or an original responding BDS with known modes. `may` never becomes a `P-FORMAT` blocker. |
| `default.get` | `verb` only | `operation:"default.get"`, `default:string?`, `resolution:"no-default"|"resolved"|"dangling"|"unknown"`. `no-default` means no configured target; `resolved` means an entry id exists or the BLS stem exists; `dangling` means lookup completed and the target is absent; `unknown` means boot-root reading or BLS discovery could not complete. |
| `default.set` | `id:string` required. | `operation:"default.set"`, `generation:u32`, `default:string`. |
| `source.detect` | `verb` only | `operation:"source.detect"`, `kind:"source.detect"`, `sources:source_candidate[]`, where `source_candidate` is `{kind:"block"|"image"|"dir",path:path,identity:string?,model:string,size_bytes:u64,boot_root:path,boot_root_present:bool,readable:bool,writable:bool,needs_privilege:bool,mounted_at:path?,why:string,export_candidate:"candidate"|"mounted"|"not_candidate"?}`. `export_candidate` is `"candidate"` for an unmounted recognized Canoe export, `"mounted"` for a recognized export that is mounted, and `"not_candidate"` otherwise. A mounted recognized export remains a candidate but is unavailable; `mounted_at` retains its mount path. |
| `bls.list` | `verb` only | `operation:"bls.list"`, `entries:bls_file[]`. |
| `bls.show` | `name:string` required. | `operation:"bls.show"`, `entry:bls_file`. |
| `bls.stage` | `name:string`, `entry:path`, `artifacts:artifact[]` required. An `artifact` is `{source:path,destination:string,sha256:string}`. | `operation:"bls.stage"`, `receipt:{name:string,artifacts:string[]}`. |
| `slot.status` | `slot:string?`, `bootctl_output:string?`, `gpt_active_slot:string?`. | `operation:"slot.status"`, `active_slot:"a"|"b"?`, `inactive_slot:"a"|"b"?`, `source:string`, `installed:("a"|"b")[]`. |
| `install` | `staged:path` required; `slot:string?`, `both:bool?`, `inactive:bool?`, `i_know_inactive_status:bool?`, `active_slot:string?`, `bootctl_output:string?`, `gpt_active_slot:string?`, `mode:u8?`, `allow_new_signer:bool?`, `boot_root_source:path?`, `id:string?`, `from_mode:0|1|2|null?`, `prior_canoe:bool?`, `acknowledge:string[]?`, `current_vbmeta:path?`, `target_vbmeta:path?`, `target_image:path?`, `staged_loader_bytes:u64?`, `staged_loader_sha256:string?`, `staged_gm2p_bytes:u64?`, `staged_gm2p_sha256:string?`, `staged_tzmap_bytes:u64?`, `staged_tzmap_sha256:string?`, `staged_tools:file_identity[]?`. `prior_canoe` defaults to `false`; persisted entry/config evidence remains authoritative. Effective mode is request `mode`, persisted config mode, explicit `from_mode`, or mode 0 only for an empty root. |
| `vbmeta.extract` | `image:path`, `output:path` required. The image must carry an `AVBf` footer; a standalone AVB0 blob is refused. The footer's vbmeta range is copied atomically to `output`. | `operation:"vbmeta.extract"`, `receipt:{output:path,bytes:usize,vbmeta_offset:u64,vbmeta_size:u64}`. |
| `vbmeta.check` | `image:path`, `vbmeta:path`, `partition:string` required; `tools:path?` optionally selects the `mode2_profile` worker. `image` may be standalone AVB0 or an AVBf-footed partition image; `vbmeta` is the main standalone AVB0 image. The worker walks chain descriptors only, so duplicate build properties do not refuse this key comparison. | `operation:"vbmeta.check"`, `partition:string`, `key_matches:bool`, `image_key_sha256:string`, `chain_key_sha256:string`, `rollback_index_location:u32`. |
| `ota-apply` | `staged:path` required; `target_slot:string?`, `bootctl_output:string?`, `gpt_active_slot:string?`, `mode:u8?`, `allow_new_signer:bool?`, `boot_root_source:path?`, `id:string?`, `from_mode:0|1|2|null?`, `prior_canoe:bool?`, `acknowledge:string[]?`, `current_vbmeta:path?`, `target_vbmeta:path?`, `target_image:path?`, `staged_loader_bytes:u64?`, `staged_loader_sha256:string?`, `staged_gm2p_bytes:u64?`, `staged_gm2p_sha256:string?`, `staged_tzmap_bytes:u64?`, `staged_tzmap_sha256:string?`, `staged_tools:file_identity[]?`. `prior_canoe` follows `install`; mode 1 requires target vbmeta evidence before writing. A global CLI `--source` remains authoritative over `boot_root_source`. | `operation:"ota-apply"`, `receipt:install_receipt`, `acknowledged:string[]`, `warnings:string[]`. |
| `vbmeta.graft` | `vbmeta:path`, `recovery:path`, `output:path` required. Legacy request aliases `graft` and `vbmetaport` are accepted. | `operation:"vbmeta.graft"`, `receipt:{output:string,bytes:usize}`. |
| `vbmeta.inspect` | `vbmeta:path` required; `tools:path?` optionally selects the `mode2_profile` worker directory. When omitted, the worker is resolved from `CANOE_TOOLS_DIR`, then the executable's directory, then `PATH`. | `operation:"vbmeta.inspect"`, `rollback_index:u64`, `chain_partitions:{rollback_index_location:u32,partition_name:string,public_key:u8[]}[]`, `build_properties:{system_os_version:string?,system_security_patch:string?,vendor_security_patch:string?,boot_security_patch:string?}`. Chain partitions whose name starts with `vbmeta` are excluded; `recovery` is ordinary. Identical repeats of a named build property are accepted without rewriting the input; conflicting values return `vbmeta-duplicate-property`, never a partial result. |
| `vbmeta.header` | `vbmeta:path` required; `tools:path?` optionally selects the `mode2_profile` worker directory. | `operation:"vbmeta.header"`, `algorithm_type:u32`, `rollback_index:u64`, `flags:u32`, `release_string:string`, `public_key_sha256:string?`, `build_properties:{system_os_version:string?,system_security_patch:string?,vendor_security_patch:string?,boot_security_patch:string?}`. These are raw AVB header fields; clients classify `algorithm_type` themselves (`0` means tree-built; nonzero means signed or grafted). The signer digest identifies a key only; it does not prove OEM identity, authorization, or a safe rollback. This additive response extension keeps protocol version 1. |
| `vendorboot.patch` | `input:path`, `output:path` required. Legacy request alias `vendor_boot.patch` is accepted. Patches both the kernel blacklist and Android `modules.blocklist`, including upgrades from cmdline-only images. Supports v3/v4 with single-archive newc/crc CPIO, gzip or legacy LZ4 ramdisks. Keeps image size and section offsets fixed; growth is limited to existing zero padding. Existing blocklists and unrelated CPIO records are preserved. AVB signatures are not regenerated. | `operation:"vendorboot.patch"`, `receipt:{output:string,bytes:usize,changed:bool}`. |
| `fastboot.identify` | `timeout_seconds:u64?` (default `10`, shared across all queries). | `operation:"fastboot.identify"`, `bds_version:string?`, `current_slot:"a"|"b"?`, `devinfo:string?`, `last_launch:string?`, `is_userspace:bool?`, `boot_root:string?`. The optional `devinfo` and `last_launch` values are raw BDS strings and are carried verbatim when available. `boot_root` is the BDS's own boot-root observation for this boot: one of `populated-config`, `populated-managed`, `empty-root`, `no-root`, or `no-volumes`; it is absent when the BDS did not publish it. `is_userspace` is `true` when `getvar is-userspace` reports `yes`, `false` when it reports `no`, and `null` when the variable is missing, unsupported, or has an invalid textual value. The server never guesses a slot or fastboot mode. A missing fastboot executable returns `fastboot-unavailable`. Local wait/read failures, invalid UTF-8, and unsuccessful non-remote command exits return `operation`, with the failing `getvar` command and cause in `error.message`; corrupted identity is never accepted. A genuine probe deadline or successful probes with no usable reply return `no-response`. |
| `fastboot.export` | `target:string?` (default `"persist"`), `timeout_seconds:u64?` (default `30`). | `operation:"fastboot.export"`, `node:string`. The server adopts an existing unmounted Canoe export or starts one for `target`, then returns its raw block node. |
| `fastboot.end-export` | `node:path` required. | `operation:"fastboot.end-export"`, `node:string`. |
| `fastboot.fetch` | `partition:string`, `output:path` required. | `operation:"fastboot.fetch"`, `partition:string`, `output:string`, `sha256:string`, `bytes:u64`. |
| `fastboot.abl-coverage` | `tools:path?`; `timeout_seconds:u64?` (default `30`). | `operation:"fastboot.abl-coverage"`, `slots:{slot:"a"|"b",coverage:"vulnerable"|"stock"|"unknown"}[]`. The server fetches and probes `abl_a` and `abl_b` without flashing or staging; `vulnerable` is `build --probe` reporting `gbl_patched:true`, `stock` is `false`, and a fetch or probe that cannot answer is `unknown`. |
| `fastboot.flash` | `partition:string`, `image:path` required. The image must be an existing non-empty regular file; `expected_bytes:u64?`, `expected_partition_bytes:u64?`, `expected_sha256:string?` optionally review its identity before flashing. `expected_partition_bytes` requires the target and source image to match the supplied complete partition size before a flash; clients must capability-gate this whole-partition safety contract with `whole-partition-write-v1`. | `operation:"fastboot.flash"`, `receipt:{partition:string,image:string,bytes:u64,sha256:string}`. The receipt's `image` is the requested source path; fastboot receives an internal private staged copy whose exact reviewed byte range is verified before spawning. |
| `fastboot.reboot` | `target:"bootloader"|"fastboot"|"recovery"?`. Omit `target` for a normal reboot; any other value is refused. | `operation:"fastboot.reboot"`, `target:"bootloader"|"fastboot"|"recovery"?`. |
| `system.reboot` | `target:"system"|"recovery"` required. The server starts `reboot` (or `reboot recovery`) and returns before the reboot takes effect; the caller may never receive this response. | `operation:"system.reboot"`, `target:string`. |

### Reviewed install inputs and OTA slot authority

`install` and `ota-apply` require a non-empty `staged_tools:file_identity[]` inventory whenever `<staged>/tools` contains files. The inventory is matched by tool file name, byte length, and SHA-256, and its count must agree exactly; the staged directory path itself is not an identity. The server copies exactly the captured byte length from each retained source handle into private staging and re-hashes that private copy before committing or flashing it.

For `ota-apply`, both inferred and explicit targets require authoritative active-slot metadata. `bootctl_output` supplies that authority only through an explicit `current-slot` or `active-slot` marker; unrelated numeric status fields are ignored. An explicit target with unknown authority returns `ota-active-slot-unknown`.

The version-1 golden transcripts are in `tests/fixtures/protocol/`. `tests/protocol_fixtures.rs` replays every request against the built binary and compares complete response bytes, including field order and the trailing newline. Update every affected transcript from the real binary before running this strict replay.
