# b5 firmware reconstruction and qualification

## Preserved base

- b4 checkpoint: `7.0.0-b4-final`, `55223acc09cb9aa376235962b62ac39febe4e644`.
- New upstream base: `superturtlee/gbl_root_canoe/main`, `74ed59f0994f019ff37334ee5da298892a46290c`.
- b5 starts at upstream and reconstructs final b4 behavior in seven semantic commits.
  The old sequence of reversals and amendments is not replayed.
- Immediately after those seven commits, the complete tree differed from b4 in
  exactly three files: removal of unused CardInfo lookup, partition limit 256
  (b4 used 192), and upstream's shutdown/reset-reason/display changes. All other
  tracked b4 paths were restored byte-for-byte, including intentional deletions.

## Intentional b5 changes

- Separate manual and managed USB DXEs, checked export ownership, no managed-to-manual fallback.
- Streamed storage SHA256 through pinned AOSP AVB implementation; no optional firmware crypto provider.
- Version 7.0.0-b5 / code 18. Hosted app replaces desktop bundles; direct toolkit Linux/Windows
  build entrypoints refuse. Native ext4 helper remains historical source, outside
  b5 release/default test paths. No driver source changes in unrelated sibling repos.
- KSU package requires explicit `CANOE_KSU_DIST` with matching product/version/runtime
  manifest. It never fetches or packages the old b4 archive. The current Android
  worker exposes no installer deployment: shell checks `installer supported` before
  opening volume-key input and installs the manager only.
- Firmware CI builds EFI artifacts; checkpoint tags no longer publish old desktop
  assets. Final release remains gated on the new browser/Android/hardware matrix.

## USB/fastboot contract

`getvar canoe-hash` returns `sha256-range-v1`. The command is
`hash:<partition>:<hex-offset>:<hex-length>`, with all three operands required.
Offsets and lengths have 1–16 hexadecimal digits without 0x. The partition name is
an explicit GPT name; no slot switching or alias guessing occurs. Zero length
hashes the empty range at a valid offset. Overflow/out-of-bounds/geometry errors
fail before reads. Data is flushed, then read from Block I/O in aligned windows,
including byte-granular start/end ranges. It never hashes the download buffer.
The reply is `OKAY` plus 43 unpadded base64url SHA256 characters (47 bytes total),
within the standard 64-byte fastboot frame. Clients decode 32 bytes for normal hex
presentation. SHA source provenance is in `FastbootLib/AvbSha/README.md`.

`getvar canoe-managed-storage` returns `bot-v1` only with an embedded managed
variant. `oem managed-storage:persist`, `:logfs`, `:boot-root`, or
`:boot-root:<32-hex-container-identity>` choose a single export. Existing
`oem mass-storage:*` commands and the physical menu retain ordinary OS mounting.

| Mode | VID:PID | Interface | Binding |
| --- | --- | --- | --- |
| Manual |1209:ca0e|08/06/50|OS mass-storage|
| Managed |1209:ca0f|ff/06/50|Interface 0-scoped WinUSB / explicit WebUSB|

Both use BOT/SCSI. Managed does not expose a protected class 08 alternate setting.
The two images are loaded separately and share one BDS export lease. BDS unmounts
its container, resolves storage after controller changes, flushes, assigns one
LUN, then starts the selected gadget. Eject completion precedes stop/unassign/
flush. Failed cleanup retains ownership; fastboot is not re-announced over it.
Managed start failure never falls back to a disk the OS could mount.

## Completed checks and remaining gates

Passed locally:

- Full native tool/patcher/UEFI/module/import contract suite (`make test`).
- Production SHA256 source against Python hashlib: empty/padding boundaries,
  unaligned reads and 536870929-byte input, plus allocation/read/flush failures.
- Actual transformed vendor start/stop/event functions: repeated sessions,
  eject-CSW ordering, partial start and failed stop followed by successful retry.
- Managed OS descriptors: wire lengths, UTF16 property, GUID and recipient/interface scope.
- Both DXEs build to AArch64 PE and pass descriptor/relocation checks (142 data
  relocation slots, 84 distinct targets each).
- Canonical `gbl_builder:latest` build produces current `BDS.efi` with both embedded
  blobs and b5 version. KSU assets stage from the actual app `dist/ksu` build.
- Final ARM64 KSU module and Android CLI toolkit rebuild successfully. Both package
  the current AArch64 Android worker and byte-identical canonical BDS image. Every
  staged WebUI asset matches the app's `dist/ksu`; its manifest is product/version/
  runtime checked. `make version-check` passes on these final packages.
- Portable image libraries pass native and WASM qualification as recorded in
  [the image-primitives matrix](b5-image-primitives.md). This is separate from
  the firmware/physical acceptance gates below.

Physical-device exports, actual Windows automatic binding, browser USB transfers
against this firmware and on-device partition hashes remain unqualified. No
physical phone operation was performed. Source/build tests do not replace these
acceptance gates.

Verified source-built blobs:

- Manual: `1268a93adeea37b38b4c08f3891117e28ae6bd5d9c65124b272fa6a0bcf98256`.
- Managed: `bdf5b6464436bd8e07fd16c184f2b2c66a2a343fa2d1975f944aadffb1708e12`.

Canonical b5 BDS build SHA256:
`dfb49203d37a18bcb425a2de8896f788081043510c06b4134cdc2792360d2e14`.

Windows binding is provided by Microsoft OS **1.0** descriptors, not an inference
from the vendor interface class: the managed variant responds to the `MSFT100`
string request, advertises `WINUSB` through its interface-0 compatible ID, and
provides its single interface GUID property as REG_SZ. Both device and interface
recipient forms of that property request are accepted only for interface 0. The manual variant has none of these.
Descriptor wire tests pass; actual Windows automatic binding still needs an
enumeration session with this firmware.
