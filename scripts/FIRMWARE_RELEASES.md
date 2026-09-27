# Firmware CI and releases

`main` and pull requests select the same firmware contracts and portable image
checks as before. Host-only contracts and portable Rust qualification run in
parallel with the Docker/EDK2 firmware job. The Docker builder image is cached
by the exact `Dockerfile` hash. Firmware CI cross-compiles the rooted-Android
commands with the NDK and assembles the one-shot archive from those commands
and the same EFI output. The generated assets are copied into
`.work/firmware-release` and verified before upload. KSU/WebUI packaging stays
in Canoe Boot Manager; this workflow does not package an old WebUI or desktop
sidecars.

An existing `release-<CANOE_VERSION>` tag triggers `release.yml`. Its version
must match `version.mk` at that exact tag commit. Manual dispatch accepts the
same existing tag. Neither workflow creates or moves a tag.

For a release, `release.yml` first looks for a successful `build.yml` push run
on `main` whose head SHA is the exact tag commit. It reuses only that run's
unexpired, commit-named firmware artifact, and only after
`scripts/firmware_release.py verify` confirms the complete BDS, tool, one-shot,
manifest and checksum set. If no qualifying artifact remains, the release calls
the full firmware workflow at the tag commit. The selected artifact is
downloaded by its run ID and verified again before the release helper creates or
updates a **draft** GitHub release. The helper never publishes automatically,
and published releases cannot be overwritten. Qualify the exact draft assets
and publish deliberately when they are ready.

The release assets are:

- `BDS.efi`
- `ArbTools.efi`, `BLTools.efi`, `RebootTools.efi`, `SurfaceTools.efi`,
  `UsbTools.efi`, `LogTools.efi`, `MdTools.efi`, `CrashTools.efi`
- `canoe-one-shot-<CANOE_VERSION>-android-arm64.zip`
- `manifest.json` and `SHA256SUMS`

The manifest extends the manager's existing firmware catalogue shape:

```json
{
  "schemaVersion": 1,
  "product": "canoe-bds",
  "version": "7.0.0-b5",
  "tag": "release-7.0.0-b5",
  "source": "<exact 40-character Git commit>",
  "dirty": false,
  "bytes": 123,
  "sha256": "<BDS.efi SHA-256>",
  "tools": [{"name": "ArbTools.efi", "bytes": 123, "sha256": "<SHA-256>"}],
  "oneShot": {
    "name": "canoe-one-shot-7.0.0-b5-android-arm64.zip",
    "bytes": 123,
    "sha256": "<ZIP SHA-256>"
  }
}
```

`tools` contains all eight tools. `oneShot` records the exact archive built by
`make target_one_shot_android`. The release helper rejects it unless its
`BDS.efi` and five staged EFI tools are byte-identical to this firmware build
and its commands are ARM64 Android executables. `SHA256SUMS` covers all nine EFI
files, the one-shot archive, and the manifest itself. Main CI produces a
candidate manifest with the intended tag name; that field does not claim that a
tag or published release already exists.

Consumers must pin an explicit published firmware tag and the reviewed byte
identities. Do not use `latest`, infer that a matching version makes bytes
interchangeable, or silently replace a prior approved firmware catalogue.
The current EDK2/Docker build is not promised to be byte-reproducible: relinking
can change output, and the existing compiler installation uses external package
repositories. The manifest records the actual build being qualified. Updating
the manager's firmware pin is a separate deliberate change after qualification.

The one-shot ZIP is part of this same firmware draft, not a desktop toolkit or
a separately mirrored manager asset. It is for a rooted Android shell with
mounted ext4 persist. The active slot's stock ABL and vbmeta are accepted as
read-only derivation sources for the prepared slot loader and sidecars. The
installer does not unlock a bootloader and never flashes an ABL. It requires
explicit `--mode`, `--persist-mount`, and `--work-dir` arguments, prints a
no-write plan unless `--apply` is supplied, then creates `persist/efisp.fat`
with `canoe-provision` and writes BDS only to raw `efisp`. The creation
primitive refuses an existing `persist/efisp.fat` rather than overwriting it.
The unused work directory holds temporary staging and readback diagnostics,
not a rollback backup.

An independent off-device persist backup and a separate recovery path are
recommended before applying the plan in case the raw `efisp` write fails.
The installer neither saves the old raw `efisp` nor provides a built-in
rollback copy.

That preparation is not a bootable chain when the active slot still has stock
ABL: stock ABL has no `efisp` redirect. The user or another separately
authorized tool must independently obtain and install a compatible signed
vulnerable ABL before Canoe can boot. The prepared `boot_<slot>.efi` is modified
and unsigned; it is a boot-root artifact, never an ABL partition payload.

Local commands, without creating releases or tags:

```sh
python3 -m unittest discover -s scripts/tests -p 'test_firmware_release.py'
make target_one_shot_android
python3 scripts/firmware_release.py package
python3 scripts/firmware_release.py verify .work/firmware-release
```

`package` requires clean tracked source, existing output from the canonical
firmware build, and the matching
`targets/one_shot_android/build/canoe-one-shot-<CANOE_VERSION>-android-arm64.zip`.
CI starts from a clean checkout and runs `make ... clean` before building, so it
cannot label a stale local binary with the release commit. Packaging and draft
upload do not contact a phone, flash a partition, or reboot.
