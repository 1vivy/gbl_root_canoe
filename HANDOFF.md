# canoe-boot-manager-webapp — Handoff

Worktree `/home/vivy/Projects/efisp-projects/gbl_root_canoe/.work/gui-work`, branch `gui-work`, HEAD `0e2aaa1`.
**Nothing is committed. All work is uncommitted in the working tree.**

Plan: `.omo/plans/canoe-boot-manager-webapp.md`

This document states the goal, what is already in the tree, where the plan's premises turned out wrong, and what remains. It is not a procedure — the plan holds the detail, and `git diff` holds the truth about the code.

---

## 1. End goal

One Svelte 5 + Vite static SPA in a **new repository**, building a single `dist/` consumed by both:

- a Tauri 2 desktop shell, and
- the KernelSU Android WebUI.

`canoe-bootmgr` stays the single writer and the only owner of policy, derivation, and privileged device I/O. The UI speaks **only** the JSON wire protocol — no library linkage, no second config writer, no policy in the front end.

Everything else in the plan exists to make that possible: version the protocol, add the verbs the UI needs, then prove sufficiency by deleting the `canoe_bootmgr` dependency from the existing `canoe-gui` and having it still build.

---

## 2. Reconcile before executing

Three things describe this work and they disagree:

| | what it is | how far to trust it |
|---|---|---|
| `.omo/plans/canoe-boot-manager-webapp.md` | the plan | written before much was measured; several rows rest on wrong premises (§4), and its ordering assumes rows landed in a sequence they did not |
| this document | corrections + current state | accurate as of writing, deliberately incomplete on "how" |
| the worktree | the actual code | authoritative for what exists; `git diff` is the only complete record |

**Precedence when they conflict: measured artifacts and repo state > this document > the plan's prose.**

Reconcile the three into one coherent statement of intent, then work from that. Where a measurement invalidates a row's premise, re-derive the row rather than satisfying its literal wording — §4.1 is a case where following the plan's text would encode a wrong comparison into a safety-facing surface.

Rewriting parts of the plan is the expected outcome, not a deviation.

---

## 3. What is in the tree

**Build:** `tools/canoe-bootmgr` fails `cargo --locked` — an in-flight change added `wait-timeout v0.2.1` and the lock was never regenerated. `cd tools/canoe-bootmgr && cargo fetch` resolves it. `tools/mode2-profile` and `tools/canoe-gui` both compile as-is. Always pass `--manifest-path`; a bare `cargo` at the worktree root creates a stray root-level `target/` that `.gitignore` does not cover (it only ignores `tools/*/target/`).

**Landed:**

- `tools/canoe-bootmgr/PROTOCOL.md` — new. Envelope, both transports (`--json` JSONL and one-shot `--request-b64`), 64 KiB request / 1 MB response limits, error-code table, 27-verb catalogue, additive-only rule.
- Verbs: `protocol.version`, `fastboot.identify`, `fastboot.export`, `fastboot.flash`, `fastboot.reboot`, `fastboot.abl-coverage`, `vbmeta.inspect`, plus an `export_candidate` field on `source.detect`.
- Golden request/response fixtures under `tools/canoe-bootmgr/tests/fixtures/protocol/`, replayed by `protocol_fixtures.rs`.
- BDS (`submodules/uefi`): `canoe-devinfo` and `canoe-last-launch` fastboot variables; a GPT slot-attribute reset behind one `oem` verb; extended host test suite.
- `tools/mode2-profile`: AVB chain-partition (tag 4) descriptors, four named build properties, `rollback_index` @112, a header-only reader, duplicate-property refusal.
- `wiki/docs`: slot-switch-as-recovery advice removed, EN and zh.

**Half-finished — both compile, neither is complete:**

- **Protocol-only `canoe-gui`.** `canoe_bootmgr` is at zero references in `tools/canoe-gui/Cargo.toml` and the crate builds. 28 files, 654+/656−. This is the sufficiency proof and it holds at type-check level. Outstanding: `cargo test`, `canoe-gui --smoke`, and the three-branch behaviour in §4.6.
- **`vbmeta.inspect` spawn-vs-link.** The `mode2_profile` path dependency is already removed from `canoe-bootmgr/Cargo.toml`, and `mode2-profile/src/main.rs` gained an `inspect` subcommand (~173 lines). Outstanding: wire `vbmeta_inspect.rs` to resolve and spawn the worker through the existing resolver in `build_tools.rs:37-70`, then regenerate the lock. Why it matters: §4.5.

**Not started:** `5b` (`default.clear` + repair-only-dangling), `1b` (error taxonomy), `6d` (`mode.plan`), wiring `canoe-devinfo` into `fastboot.identify`, and the whole new-repo standup / surfaces / switchover.

Raw working notes from the run are under `.omo/evidence/` and `.omo/ulw-execute/ledger.jsonl` — background only, nothing depends on them.

---

## 4. Where the plan's premises are wrong

These change *what must be done*, which is why they are here rather than in a report.

### 4.1 Graft is recovery-to-recovery, and the recorded comparison used the wrong pair

Graft splices signature material **from a stock recovery into a custom recovery**:

- **donor** = stock recovery — the OEM-signed source
- **candidate** = custom recovery — the tree-built image being made bootable

It is **not** derived from `vbmeta.img`. Treat a vbmeta as an input only if that is separately established.

The comparison recorded during this work read `candidate_vbmeta.img` against `candidate_custom_recovery.img` — using a vbmeta belonging to the *candidate* ROM as the "stock" side, and labelling it stock. The byte readings are real but the pair is not the graft pair:

| file read | algorithm_type | flags | release_string |
|---|---|---|---|
| `candidate_vbmeta.img` — candidate ROM's vbmeta, **not a donor** | 2 (SHA256_RSA4096) | 0 | `avbtool 1.3.0` |
| `candidate_custom_recovery.img` — candidate recovery | 0 (NONE) | 0 | `avbtool 1.3.0` |

The correct pair is now on disk in `~/Downloads/gbl7_op15/`:

```
donor_recovery.img              104857600   donor: stock recovery
candidate_custom_recovery.img   104857600   candidate: custom recovery
candidate_vbmeta.img                12288   candidate ROM vbmeta — not part of the pair
```

**What must be done:** establish the differentiator from `donor_recovery.img` vs `candidate_custom_recovery.img`. Mechanically the same read path — a recovery image carries an `AVBf` footer pointing at an embedded vbmeta (observed on the candidate: `vbmeta_offset=67080192, vbmeta_size=832`), and the fields sit at fixed offsets `algorithm_type`@28, `rollback_index`@112, `flags`@120, `release_string`@128, so no descriptor walk is needed.

**What probably survives but must be re-confirmed, not inherited:** that tree-built declares `algorithm_type` 0 (NONE) while OEM-signed declares a real algorithm, and that `flags` and `release_string` do not discriminate. Both held for the pair actually read, but its stock side was the wrong file. Anything already coded against the old conclusion — including classifier confidence ordering in `tools/mode2-profile` — needs re-checking.

**Independent of the outcome:** a tree-built image signed with a *non-OEM* key lands in the "signed" bucket and raises no warning from this signal alone. The public-key chain-descriptor diff is the real guard, so any surface must show the raw signal alongside a verdict, never the verdict alone.

### 4.2 Format prediction has almost no real input on this hardware

`candidate_vbmeta.img` contains a genuine duplicate of `com.android.build.boot.security_patch` (offsets 5560 and 5648, identical values `2026-08-01`, corroborated by `avbtool info_image`). Because duplicate named properties are refused — descriptor order must not change a format decision — **full descriptor inspection can never succeed on that image.** Header-only reads are unaffected.

That image also carries **none** of `com.android.build.system.os_version`, `.system.security_patch`, `.vendor.security_patch`.

So `mode.plan`'s format prediction and the first-install format step must treat **"cannot predict" as a first-class outcome**, reached both by refusal and by absent properties. The plan implies prediction is normally available; on real hardware it frequently is not.

Descriptor census for that image: 18 total — 9 property, 2 hashtree, 2 hash, 5 chain-partition (`boot`, `dtbo`, `recovery`, `vbmeta_system`, `vbmeta_vendor`).

### 4.3 Duplicate refusal is deliberately narrow

The refusal covers only the four format-decision keys. Legacy `com.android.build.boot.os_version` keeps last-value-wins on purpose: it feeds the legacy GM2P derivation, whose 120-byte output must stay byte-identical. Widening the refusal breaks that. Intentional, commented, test-pinned.

### 4.4 BDS published state

```
canoe-devinfo: unlocked=<0|1> critical=<0|1> waiver=<0|1|unknown> retry_a=<0-7> retry_b=<0-7>
canoe-last-launch: requested / effective / reason
```

`unknown` is first-class in both — never fabricate a value. `canoe-last-launch` is written to the `logfs` GPT partition (`SuperFbLogFlush.c:56-127`, file `\canoe`) at the final mode resolution point (`SuperFbLaunchPolicy.c:299-311`, after the lockstate demotion, after `LoadImage` succeeds, before `StartImage`) and published at the next fastboot entry. A launch that never ran is never recorded. Values are a per-session snapshot, so a mid-session reset leaves them stale until the next fastboot session.

### 4.5 `vbmeta.inspect` must spawn, not link

`build` resolves `mode2_profile` via `--tools` → `CANOE_TOOLS_DIR` → exe dir (`build_tools.rs:37-70`) and spawns it (`:121-129`). Linking the crate instead means pointing `--tools` at a toolkit holding a different `mode2_profile` gives you two different AVB parsers inside one binary depending on which verb is called. It also removes no shipped binary, since `build` still needs the worker.

When this crosses the subprocess boundary, the duplicate-property refusal must keep its distinct identity rather than flattening into a generic failure — `mode.plan` and first-install need to tell that specific case apart from any other error. A subprocess also introduces two things that did not exist before: a hanging worker must yield a bounded error, and the image path now reaches a spawn.

### 4.6 `export_candidate` is three-state for a reason

`"candidate"` / `"mounted"` / `"not_candidate"`, with `mounted_at` alongside. It replaces `canoe-gui`'s `diagnose_rejection` (`export.rs:110-143`), which distinguishes *wrong device* from *right device currently mounted* and emits "already mounted at X; unmount it before attaching" for the automount case — the common desktop failure.

A two-state boolean collapses those and loses the diagnostic, and because the sufficiency proof is a *compile* check it would still pass. The protocol-only `canoe-gui` must consume the field directly, not reconstruct the mounted case from `kind` + `mounted_at` (that reconstruction leans on an undocumented detector invariant).

Keep `is_export_candidate` observably unchanged — it means "attachable now" and is load-bearing for `find_export_node` (`operations.rs:248`), `export_drive.rs:117`, `export_control.rs:101`.

### 4.7 Errors are not yet distinguishable

All operation failures collapse to code `operation`, exit 1 (`cli_runner.rs:64-69, :91-100`). A client cannot separate permission from missing-source from malformed from timeout. This is what `1b` exists for, and the self-diagnosis design in later rows depends on it.

Concrete case to fold in: `fastboot.identify` returns `ok:true` with null fields when **no fastboot binary exists at all**, so "device silent" and "tooling missing" are indistinguishable.

### 4.8 Hazards that constrain later work

- **`fastboot_command.rs` timeout is not airtight.** It kills the direct child then blocks on a stderr-reader `join()`; a grandchild inheriting the pipe delays the envelope past the timeout (3014 ms against a 1 s timeout with a forking fake; 30014 ms with a `sleep 30` grandchild). With a real single-process fastboot the bound holds and the outcome is always an error, never a false success. Pre-existing; natural to harden alongside `1b`.
- **Partition names are unvalidated.** `../../boot` and shell-metacharacter names reach the spawn as-is. Safe today only because no shell wraps it (`$(touch /tmp/x)` arrives as literal argv). Nothing enforces that going forward — relevant to the flash-grafted-image surface.
- **`include_bytes!` bakes fixtures in at compile time.** Restoring a fixture file does not invalidate the build; `touch` or clean before trusting a green result.
- **The BDS image build is whole-image non-deterministic** — two clean container builds from identical sources differ in 27,223 bytes across every 4 KiB block. Whole-image sha256 equality is not a provenance check and a mismatch is not evidence of tampering. Forced single-object recompiles *are* reproducible.

### 4.9 GPT slot-attribute reset

The entry point takes **no slot argument** and re-reads the active slot at call time. Reachable only from one explicit `fastboot oem` verb — not automatic, not a menu item, not reachable from the web app. Setting PRIORITY|ACTIVE on a *named* slot is the reference `SetActiveSlot`'s new-slot half without its alternate-slot fixup; aimed anywhere but the active slot it yields a both-slots-active GPT or a slot switch. **Do not add a slot parameter.**

Bit math and the offline GPT check are automated. The device-level rungs are operator-run and **have not been performed**; the procedure and its abort condition are written into `.omo/evidence/task-5d-*.txt`. Note the toggle test needs *two real writes* with a net-zero result — writing an identical value back does nothing, because the flush is gated on a `CompareMem` diff.

**Unresolved:** which boot a retry reset protects. The reference ABL flow suggests the decrement precedes handoff; Ghidra RE of the real chain suggests the decrementing agent is the patched ABL *downstream* of the BDS, meaning a reset could affect the current boot. No surface may claim which boot a reset protects until that is settled by observation.

---

## 5. What remains, and against what

| Goal | Against |
|---|---|
| Finish the sufficiency proof | `tools/canoe-gui` — run its tests and smoke; make `diagnose_rejection`'s three branches come from `export_candidate` (§4.6) |
| Finish `vbmeta.inspect` as a spawned worker | `vbmeta_inspect.rs` + the resolver at `build_tools.rs:37-70`; keep the duplicate refusal distinguishable (§4.5) |
| Re-establish the graft differentiator | `donor_recovery.img` vs `candidate_custom_recovery.img` (§4.1); update whatever in `mode2-profile` encoded the old conclusion |
| Make "no default" representable | `config_ops.rs` — `clear_default` is currently undone by `repair_default` on the next generation bump; a deliberately-absent default must survive, while a *dangling* one is still repaired |
| Give failures machine-readable identity | `cli_runner.rs`, `errors.rs` (§4.7), without breaking the WebUI's parsing of existing messages |
| Make a mode change a planned transition | `mode.plan` over `vbmeta.inspect` output, with "cannot predict" as a real outcome (§4.2) |
| Expose devinfo through the protocol | requires changing the `Identity` type that `fastboot.identify` returns |
| New repo standup | Svelte 5 + Vite + Tailwind + shadcn-svelte, one static `dist/` with relative asset paths, ES2018 target for older Android WebViews; Tauri 2 shell with `canoe-bootmgr` as a sidecar |
| Two transport adapters | desktop: spawn `canoe-bootmgr --json` as a sidecar, elevation decided *before* attaching, never as error recovery. device: one-shot `--request-b64` through `ksu.exec` |
| Surfaces, then switchover | per the plan's later waves, once the above holds |

**Sequencing that still applies:** every verb touches `wire.rs`, `wire_command.rs`, `cli.rs`/`cli_extra.rs`, `output.rs`, `operations.rs`, plus a `PROTOCOL.md` row and a golden fixture — so verb-adding work serializes on those files, and the catalogue count must stay balanced (currently 27 verbs / 27 doc rows; the 6-row error-code table is not verbs). The new-repo work is gated on the sufficiency proof: if `canoe-gui` cannot build without the crate, the protocol is still insufficient and the split should not proceed.

---

## 6. Standing constraints

- `canoe-bootmgr` is the single writer and sole owner of policy, derivation, and privileged device I/O. No second config writer. No policy in the UI.
- Protocol changes are additive only — new verbs, new **optional** fields. Anything else bumps the protocol version.
- `targets/magisk_module/module/webroot/protocol.js` mirrors the protocol and must stay valid.
- Do not move `targets/magisk_module`, `canoe-bootmgr`, `canoe` CLI, `canoe-ext4`, `submodules/uefi`, `submodules/patcher`, `ablrepo`, or `wiki`.
- Never flash a device or run destructive fastboot/adb as part of verification. Device paths go through the fake-script harness in `tools/canoe-bootmgr/tests/fastboot.rs` — hold its `SPAWN_LOCK` guard across the exec or you get intermittent `ETXTBSY`.
- Container builds run with `--user "$(id -u):$(id -g)"`; delete `submodules/uefi/edk2/Conf/BuildEnv.sh` first if a prior run left a foreign path in it.
- The other slot's health is unknown. Never present it as a fallback, recovery, or unbrick path; the only legitimate slot switch is the officiated OTA one.
- Do not commit or push without being asked.
