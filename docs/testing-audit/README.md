# First-party test ownership audit

This audit starts at `c5394c077eafc1e3f76a62a797e4cd80f473a373` and changes test ownership, assertions and CI duty selection. It does not change firmware behavior, filesystem policy, dependency pins or published release references.

The [case inventory](inventory.tsv) records every existing named Rust/Python test and each C test function called by its driver. Monolithic C programs and shell/qualification drivers are parameterized cases, with their dimensions described by the contract. They are not counted once per assertion or loop iteration. Imported upstream EDK II suites are excluded; first-party tests of the imported Ext4Pkg patch set are included. Rows for deleted groups retain their original source location. Locations refer to the audit baseline, except the two new CI selector cases.

The 307 rows comprise 305 baseline cases/groups and two new CI decision tests: 289 keep, four move, nine rewrite, five delete. This is an inventory count, not a runner's pass count. Historical and explicitly gated fixtures remain marked as such; inventory inclusion does not make them release acceptance.

## Decisions applied

| Area | Change | Contract retained |
| --- | --- | --- |
| BDS menu | Remove fixed caption, color, pixel position, repaint count and generated-label expectations; remove the duplicate rendered-mode matrix | Real key dispatch, held-key handling, countdown cancellation, selection retention, startup policy, unsafe automatic-boot guards, output bounds |
| BDS entry discovery | Stop requiring generated English titles and action ordering | Available actions, discovered loaders, mode/default behavior and stale-slot automatic-boot suppression |
| Fastboot response | Remove the old failing SafeString implementation model and exact packet-count assumption | Production framing reconstructs complete long responses within packet/canary bounds, including observed DeviceInfo values |
| GPT | Delete the independent Python rewrite and its shell roundtrip | `test_slot_retries.c` executes shipped `SuperFbSlots.c`, checking retry bits, unrelated bytes and write/flush failures |
| Filesystem primitives | Move four Root/staging tests from `canoe-bootmgr` to `canoe-fs` | Confined access, retained-parent publication, replacement refusal, ownership across close/reopen |
| Module installation | Remove exact English/Chinese guidance and device-summary prose assertions | Manager-only install/update, unchanged partition/root sentinels, no deployment/reboot command, language preference preservation |
| Import/evidence metadata | Remove a generated comment check and fixed number of evidence tables | Stable generated variables and parsing of every shipped table |
| CI | Select checks by their owning component | Release, manual and unclassified build inputs retain complete qualification |

The four moved filesystem tests call `canoe_fs::confined` directly; their previous location tested only a re-export. The two remaining boot-manager confinement cases exercise `LocalDir` and configuration handoff, so they stay with their caller.

A byte parser in Rust and its C firmware consumer each retain their own ABI checks. A consumer test should establish the handoff; it should not reproduce the producer's algorithm. Browser FAT/ext4 internals belong to their dependency repositories. The UEFI Ext4Pkg adapter and its imported patch set are different production code, and remain tested beside that driver.

## Before adding a test

Before adding a test, identify the production contract, owning repository and smallest suitable test layer. Reuse existing coverage. Do not encode subjective acceptance, incidental presentation or the current implementation as requirements. Do not add regression tests for every reversible edit by default. Use human or agent exploration for usability assessment, and report its evidence separately.

The two added `test_ci_scope.py` cases are justified by the new CI dispatcher: skipping an owning check or skipping a full release build is a production automation failure. One table covers path/dependency selection and one covers mandatory full qualification. Neither runs or duplicates those checks.

See the [scenario matrix](scenarios.md), [CI duties](ci-duties.md) and [acceptance limits](acceptance-limits.md) before choosing a layer.

## Local verification and cost

Comparable warmed commands on the same workstation:

| Check | Before | After assertion/ownership changes |
| --- | --- | --- |
| `make test` | 6.518 s | 6.508 s |
| Default Rust cases | 107 passed, two ignored | 107 passed, two ignored |
| Script unittest cases | 17 passed | 19 passed, including two new CI dispatcher cases |
| Native C drivers | 31 BDS + two patcher drivers | Same drivers; narrowed assertions |
| Test-only GPT shell/Python driver | One | Removed |

The first after-run was 6.791 s while recompiling changed fixtures. These timings do not establish a speed improvement; the warmed difference is noise. The purpose of the reductions is removing false requirements and assigning cases to the code they exercise. CI avoids substantially larger unrelated builds, documented separately.

Focused filesystem and boot-manager Cargo suites, changed menu/framing/launch fixtures, the final aggregate suite and workflow lint passed. The historical real-ABL corpus case and privileged mounted-container fixture were not enabled. `canoe-provision`'s historical `legacy-ext4-tests` feature remains disabled. No new phone or VM qualification occurred in this audit.

Use the smallest applicable existing command:

```sh
cargo test --locked --manifest-path tools/canoe-image/Cargo.toml
cargo test --locked --manifest-path tools/canoe-fs/Cargo.toml
make -C submodules/uefi test
make -C submodules/patcher test
sh targets/magisk_module/tests/test_flows.sh
python3 -m unittest discover -s scripts/tests -p 'test_*.py'
```

`make test` remains the complete local first-party aggregate. CI selects the relevant commands instead of treating every change as a firmware release.
