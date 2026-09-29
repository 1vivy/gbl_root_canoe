---
name: canoe-imports-and-release-bump
description: "Bump a Canoe release or import when imports.toml pins, generated metadata, rebuilt artifacts, or version/package gates must move together."
---

# Canoe imports and release bumps

`gbl_root_canoe` runs two development patterns. Treating them the same is what leaves stale artifacts in shipped packages.

- **Core project** — normal edit/test/commit, versioned by `version.mk`: `submodules/uefi/edk2/QcomModulePkg` (the BDS), `submodules/uefi/tests`, `submodules/patcher`, `submodules/ablfvextractor`'s `extractfv.c`, the six Rust `tools/` projects (`canoe-bootmgr`, `canoe-image`, `canoe-provision`, `canoe-fs`, `mode2-profile`, `abl-tzmap`), `targets/`, `wiki/`. `tools/canoe-ext4` is tracked but historical and outside the release/default test paths; `tools/canoe` and `tools/canoe-host` are retired.
- **Imports** — arrive as squashed source, a compiled output from a sibling repo, or foreign data. Owner-authored EDK2 code is ~47.7k of the ~2.24M lines under `edk2/`, so the vendored copy is never "ours" in bulk.

## The manifest

`imports.toml` at the root declares every import, one row each; `imports.mk` is generated from it and is the only make-visible source of pins. `version.mk` keeps `CANOE_VERSION`/`CANOE_VERSION_CODE` and its override guard, nothing else.

```sh
make imports                      # table: id, kind, identity, current state
python3 scripts/imports.py check  # same check make version-check runs
make import-pin ID=<id>           # re-hash the file in the tree and rewrite the row
```

Five kinds, distinguished because they can be proven to different depths:

| kind | check proves | check cannot prove |
|---|---|---|
| `artifact` | the committed file hashes to the pin (digest **and** size) | that the producer built it from any particular source |
| `fetch` | the row names a URL and digest; `pinned = false` reports `UNPINNED` | anything local — the file is never committed |
| `subtree` | no drift since `imported_at` in this repo | upstream identity; `upstream.rev` may be `unknown` |
| `data` | each entry matches the digest it already carries (`abl.sha256`, `abl.meta`) | provenance of the entry |
| `satellite` | a project's own declared versions agree (per-INF `VERSION_STRING`) | that it belongs to `CANOE_VERSION` — it deliberately does not |
| `external` | nothing; reported `EXTERNAL` | it is operator-supplied and unpinnable |

`UNPINNED`, `DRIFT` (with `local_patches = true`) and `EXTERNAL` are reported without failing. Do not "fix" them by deleting the row.

## Release order

```sh
make bump VERSION=<version> VERSION_CODE=<code>   # regenerates version.mk, imports.mk, version.rs, module.prop, README/docs version lines
# for each import that moved: refresh it, then
make import-pin ID=<id>
make -C submodules/uefi build                     # canonical docker build; see below
make version-check
make test
```

The Web UI is two steps because the producer lives elsewhere:

```sh
cd ../canoe-boot-manager && bun run build
make -C targets/magisk_module webui-pin CANOE_WEBUI_SRC=/abs/path/to/canoe-boot-manager   # writes webui-cache/, prints the digest
make import-pin ID=webui
```

The mass-storage blob needs a BDS rebuild after pinning: it is embedded into `BDS.efi` by `embed_variant.py`, not copied at package time.

## Gate failures that mean "rebuild", not "broken"

- `BDS.efi publishes a stale canoe-bds fastboot variable` — a version bump alone leaves every object holding the previous `-DSFB_BDS_VERSION`. Rebuild; `submodules/uefi/Makefile` drops `edk2/Build/RELEASE_CLANG35` when `.canoe-version` moves.
- `package artifact mismatch: …zip carries BDS.efi <x> expected <y>` — the archive predates the loader. Rebuild that package, or remove `targets/<t>/build/` if packaging is a later release step (the gate skips a package whose build directory is absent, and reports a *missing* zip when the directory exists).
- `imports.mk: generated file is stale or hand-edited` — run `make bump` or `make import-pin`; never edit it.

## Traps that cost real time

1. **A suite can pass only where a previous build was left behind.** `test_hooks` was never recompiled locally because its binary outranked its sources, hiding a missing `-DSFB_BDS_VERSION`; `SuperFbMenu.h` `#error`s without it. The flag now appends to `CFLAGS` in `submodules/uefi/tests/Makefile` so no recipe can omit it. When a suite is green, ask whether it would be green on a clean checkout.
2. **`.gitignore`'s blanket `*.img` / `*.efi` rules swallow new fixtures.** The bootmgr's `vbmeta-inspect-*.img` AVB blobs existed only in the worktree that made them while a dozen golden protocol pairs read them by path. Exceptions are explicit and per-directory; add one when adding a binary fixture, and verify with `git check-ignore -v <path>`.
3. **Never hard-code the release version in a test or doc.** The version lives in `version.mk`, `make bump` regenerates `tools/canoe-bootmgr/src/version.rs`, `targets/magisk_module/module/module.prop` and the README/docs version lines from it, and `make version-check` re-parses every one of them — including the literal `make bump VERSION=<v> VERSION_CODE=<n>` line in `wiki/docs/release.md`, so writing placeholders there fails the gate. `make bump` rewrites that line for you.
4. **The docker build's log path must be writable by the invoking uid.** `docker run --user "$(id -u):$(id -g)" … > /out/bds_build.log` fails with `Permission denied` and `exit=1` before compiling anything if the mounted `/out` is root-owned. Use a fresh `mktemp -d`-style directory you own. Also delete `submodules/uefi/edk2/Conf/BuildEnv.sh` first if a prior run left a foreign path in it.
