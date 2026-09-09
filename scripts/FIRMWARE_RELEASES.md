# Firmware CI and releases

`main` and pull requests run the firmware contracts and portable image checks,
then build CANOE-BDS and the standalone EFI tools once with the existing
Docker/EDK2 build. The generated artifacts are copied into
`.work/firmware-release` and verified before upload. KSU/WebUI packaging stays
in Canoe Boot Manager; this workflow does not package an old WebUI or desktop
sidecars.

An existing `release-<CANOE_VERSION>` tag triggers `release.yml`. Its version
must match `version.mk` at that exact tag commit. Manual dispatch accepts the
same existing tag. Neither workflow creates or moves a tag.

The release job downloads the completed CI artifact, verifies its source
commit, PE architecture and hashes, and creates or updates a **draft** GitHub
release. It never relinks the firmware or publishes automatically. Published
releases cannot be overwritten through this helper. Qualify the exact draft
assets and publish deliberately when they are ready.

The raw release assets are:

- `BDS.efi`
- `ArbTools.efi`, `BLTools.efi`, `RebootTools.efi`, `SurfaceTools.efi`,
  `UsbTools.efi`, `LogTools.efi`, `MdTools.efi`, `CrashTools.efi`
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
  "tools": [{"name": "ArbTools.efi", "bytes": 123, "sha256": "<SHA-256>"}]
}
```

`tools` contains all eight tools. `SHA256SUMS` covers all nine EFI files and the
manifest itself. Main CI produces a candidate manifest with the intended tag
name; that field does not claim that a tag or published release already exists.

Consumers must pin an explicit published firmware tag and the reviewed byte
identities. Do not use `latest`, infer that a matching version makes bytes
interchangeable, or silently replace a prior approved firmware catalogue.
The current EDK2/Docker build is not promised to be byte-reproducible: relinking
can change output, and the existing compiler installation uses external package
repositories. The manifest records the actual build being qualified. Updating
the manager's firmware pin is a separate deliberate change after qualification.

Local commands, without creating releases or tags:

```sh
python3 -m unittest discover -s scripts/tests -p 'test_firmware_release.py'
python3 scripts/firmware_release.py package
python3 scripts/firmware_release.py verify .work/firmware-release
```

`package` requires clean tracked source and existing output from the canonical
build. CI starts from a clean checkout and runs `make ... clean` before building,
so it cannot label a stale local binary with the release commit. No command in
the build or packaging steps contacts a phone, flashes a partition, or reboots.
