# Contract scenarios and owners

This matrix is a routing guide. The individual case decisions and source locations are in [inventory.tsv](inventory.tsv). UI acceptance is separate from these executable contracts.

| Scenario | Owner / smallest layer | Existing evidence |
| --- | --- | --- |
| Startup default, temporary empty-root action, key selection, countdown cancellation | BDS / native C decisions | `test_launch.c`, `test_menu_selection.c` |
| Default and entry mode remain independent; policy-only configuration persists | BDS consumer + `canoe-bootmgr` producer / parser and file edit | `test_config.c`, `test_config_store.c`, Rust `boot_policy.rs`, `config_portable.rs` |
| Managed loader hook installs and unwinds; launch failures release resources | BDS / simulated UEFI Boot Services | `test_hooks.c`, `test_launch.c` |
| Reboot selects the intended BCB route and preserves unrelated fields | RebootTargetLib / native C ABI | `test_reboot_targets.c` |
| Missing root is different from failed reads; usable discovered root wins | BDS / native C file-probe decisions | `test_root_observation.c`, `TestBootRoot.c`, `test_launch.c` |
| Android ext4 feature/checksum compatibility and mount cleanup | Imported UEFI Ext4Pkg integration / production C with independent fixtures | `test_ext4_checksums.py`, `test_ext4_lifecycle.c`, `test_image_map.c` |
| Container mapping and managed export retain/release ownership correctly | BDS adapter / simulated Block I/O and protocols | `test_container.c`, `test_image_disk.c`, `test_msd_lease.c`, `test_raw_persist_export.c` |
| Last-launch metadata clears stale state and reports failed writes | BDS / native file writer | `TestLastLaunch.c`, `test_last_boot_store.c` |
| Long getvar responses and range hashes preserve exact wire bytes | Fastboot / C framing plus independent SHA oracle | `test_fastboot_response.c`, `test_hash.py` |
| GPT retry change preserves other fields and honors failures | SuperFbSlots / production-linked C | `test_slot_attributes.c`, `test_slot_retries.c`; no Python reimplementation |
| BLS/config syntax, options, defaults and path boundaries | `canoe-bootmgr` producer and BDS consumer / parsers | Rust config cases, `test_config.c`, `test_bls.c`, `test_linuxboot.c` |
| A selected file is staged/published through an owned confined directory | `canoe-fs` / native filesystem primitive | `files.rs`, moved `confined.rs`, moved `stages.rs` |
| Container sizing uses available space/headroom and leaves unrelated files | `canoe-provision` / container primitive | sizing unit cases; explicit mounted fixture only when needed |
| Generic graft preserves the donor envelope and respects bounds | `canoe-image` / immutable input bytes | `graft_portable.rs`; direct authentication/eligibility decisions in CBM stay with CBM |
| Android footer moves to the correct partition end; malformed metadata does not become unsigned input | `canoe-image` / byte materialization | `tests/partition.rs`; the two `src/partition.rs` unit cases instead check ABL ELF recognition |
| ABL extraction/patching and vendor_boot transformation match qualified bytes | `canoe-image`, `ablfvextractor`, patcher / producer algorithm | native unit/CLI and portable golden cases; optional cross-target qualification exports |
| Root vbmeta descriptors and exact Mode 2/TZ sidecar encoding | `mode2-profile`, `abl-tzmap`; C consumers / parser and ABI | Rust AVB/profile/evidence suites, `test_profile.c`, `test_tzmap.c` |
| Manager installation/update does not deploy or reboot | KSU package / disposable shell command sentinels | `test_flows.sh`; activated WebUI, native picker and KSU/Next guest acceptance belong to the app/harness |
| Import pin, package source and draft release identity agree | Firmware release tooling / disposable files and GH command boundary | `scripts/tests/`; no actual publication in tests |
| Path changes run relevant checks; release always runs complete clean build | Firmware CI / pure dispatcher | `test_ci_scope.py`, workflow lint; actual runner timing is separate operational evidence |

The CBM repository owns scenario selection, review/Apply scope, format-data assessment, session/transport coordination and adapter handoffs. Those behaviors should not be inferred from firmware tests. Its high-level scenarios should use the production image/filesystem interfaces and retain only evidence that those interfaces were used correctly. The owning dependency should test its own journal, extent, flush and filesystem behavior.
