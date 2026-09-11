# Firmware CI by duty

`build.yml` keeps one job and one canonical EFI build. `scripts/ci_scope.py` selects steps from changed paths rather than creating a hardware/OS matrix for ordinary source edits. A push compares its previous commit; a pull request compares the merge base. A missing baseline, unknown source/build path, manual dispatch or release `source_ref` selects full qualification.

| Change | Duties |
| --- | --- |
| Documentation / AGENTS only | Checkout and scope selection; no compiler or firmware package |
| BDS tests only | Native BDS test drivers; no Docker or EFI rebuild |
| BDS or imported UEFI implementation | Native BDS tests plus one clean BDS/tools build, version and package verification |
| Rust tool tests | Owning crate tests; no unrelated portable rebuild |
| Rust tool source/manifest/lock | Owning crate and affected image/config producers, plus existing portable qualification |
| Patcher or extractor | Owning image producer; patcher C cases for patcher changes |
| Module installer | Disposable manager-only shell fixture |
| Import/release tests | Python command tests |
| Release packager source | Python command tests and actual firmware package build/verification |
| Build definitions, CI, imports/version, unclassified paths | Complete first-party tests, portable qualification and clean firmware package |
| Release reusable call / explicit full run | Complete checks and canonical clean BDS/tools build regardless of path diff |

Rust consumer expansion follows the local crate dependencies: `canoe-fs` affects image, profile and boot-manager producers; `mode2-profile` and `abl-tzmap` affect image and boot-manager producers. Tests do not change consumers. The current retired native ext4/vbmetafixer helpers are outside packaged firmware duties. New unclassified source cannot silently skip checking.

The release caller supplies the resolved source commit, receives its artifact name, and uploads that exact captured build. It never relies on a documentation-only run's nonexistent firmware artifact. Cancellation only replaces superseded ordinary branch/PR runs; release/manual full runs are retained. Build flags, clean behavior and package provenance checks are unchanged.

## Measured baseline and expected savings

Successful [firmware run 34539914513](https://github.com/1vivy/gbl_root_canoe/actions/runs/34539914513), source `c5394c0`, reported these GitHub step durations:

| Duty | Seconds |
| --- | ---: |
| Rust toolchain setup | 9 |
| Native dependency installation | 12 |
| Aggregate first-party tests | 100 |
| Portable qualification | 46 |
| Docker toolchain build | 73 |
| Clean BDS and EFI tools build | 99 |
| Version check | 1 |
| Package upload | 2 |

The listed work totals 342 seconds, excluding checkout/job setup. Documentation-only work now skips those duties. Test/tool/module-only work avoids the 172-second Docker + EFI portion, and avoids other unrelated checks according to its scope. These are structural savings relative to that observed run, not measured new-CI wall-time results. Full releases retain the work. Local warmed aggregate tests remained about 6.5 seconds, which is not comparable to the cold GitHub runner.

The workflow and reusable release caller passed `actionlint`. Two small selector cases cover owning paths/dependent crates and mandatory full qualification. The full local aggregate remains available as `make test`.
