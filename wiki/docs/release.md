# Release runbook

CANOE-BDS and Canoe Boot Manager publish from separate repositories. Firmware CI
builds `BDS.efi` and eight standalone EFI tools. The manager repository builds
the hosted app and ARM64 KernelSU module from pinned firmware assets. It also
deploys the published hosted archive to Cloudflare Workers at
[canoe-boot-manager.1vv.ca](https://canoe-boot-manager.1vv.ca).

## 1. Prepare firmware on main

Run from the firmware repository root. `version.mk` owns the version; regenerate
its dependent files rather than editing their version fields separately:

```sh
make bump VERSION=7.0.1 VERSION_CODE=21
make test
docker build -t gbl_builder .
docker run --rm -v "$PWD:/workspace" -w /workspace gbl_builder bash -lc \
  'make -C submodules/uefi clean && make -C submodules/uefi build && make -C submodules/uefi tools'
make version-check
```

Pin any changed imported source or binary through `imports.toml` and
`make import-pin ID=<id>`. Never use a `CANOE_VERSION_OVERRIDE` local build for a
release. Existing generated package archives must match the current BDS or be
moved out of their build directories before the version gate.

Commit the release-ready source and create the requested version checkpoint tag.
The pipeline tag is `release-<version>` at that same commit. Frozen prior tags and
branches remain unchanged. Pushing the pipeline tag starts **Prepare firmware
draft release**, which runs the full firmware CI and creates a draft release.
Only a version carrying a `-suffix` is additionally marked prerelease, so 7.0.1
drafts as an ordinary release. It never publishes automatically.

## 2. Verify the exact draft assets

The draft contains `BDS.efi`, all eight EFI tools, `manifest.json` and
`SHA256SUMS`. Download those firmware assets into a dedicated directory and run:

```sh
python3 scripts/firmware_release.py verify /path/to/downloaded-firmware
```

Check the manifest's version, tag, source commit, ARM64 PE format and exact byte
identities. The helper verifies those file contracts and checksums. The local
canonical build is compile validation; a second CI build need not have identical
bytes. Qualify and consume the exact CI draft assets. Never silently substitute
a local rebuild for an approved catalogue.

Record host tests, build checks and physical-device qualification separately.
Building or publishing does not authorize flashing or rebooting a phone. Publish
the verified firmware release deliberately once the agreed checks pass.
See [firmware CI contracts](https://github.com/1vivy/gbl_root_canoe/blob/main/scripts/FIRMWARE_RELEASES.md) for the manifest
schema and draft helper.

## 3. Build and release the manager

In `canoe-boot-manager`, pin the published firmware tag, source commit and exact
artifact identities, update its package version, native source pins and module
template pin together, then rebuild and validate hosted/KSU packages. The KSU
module must contain the same approved BDS and selected tools as the hosted app.
The manager-only installer must not deploy or alter phone partitions.

Create the manager version checkpoint and matching `release-<version>` pipeline
tag at its release-ready commit. Its release workflow consumes the published
firmware assets and prepares a manager draft. Inspect the hosted archive, KSU
ZIP and release manifest before publishing. Do not revive desktop packaging or
the obsolete `webui-pin` flow.

## 4. Deploy and mirror the published package

Publishing the manager release starts **Deploy published web release**. The job
stages the already-built hosted archive, deploys it to Cloudflare Workers using
the `production` environment, and checks cross-origin isolation and firmware
identity at the configured custom domain. Manual dispatch of a published tag
supports deployment or rollback without rebuilding it.

The independent **Mirror published KSU module to firmware release** workflow uses
`FIRMWARE_RELEASE_TOKEN` to attach the verified module ZIP to its pinned firmware
release. Check both jobs and the public URL. The mirror never changes firmware
assets; a mirror failure does not undo or block the website deployment.
