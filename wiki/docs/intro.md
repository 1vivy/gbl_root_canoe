# Start here

GBL Root Canoe supports only 8gen5 (SM8845) and 8elite5 (SM8850). If you are a
beginner, do not use this project by yourself; use a qualified flashing service.

This project requires an already unlocked bootloader or a way to obtain
temporary root access. Read the device-specific safety notes in the install and
OTA guides before writing anything.

## One app, five routes

Canoe Boot Manager is one Svelte 5 application. The same pinned `dist/` bundle
is used by the Linux and Windows Tauri desktop shells and by the KernelSU
Android WebUI. The desktop package contains `bin/canoe-boot-manager` beside
`bin/canoe-bootmgr`; the app talks to that sidecar only through the JSON wire
protocol. `canoe` and `canoe-bootmgr` remain available as the native CLI and
protocol writer. No client edits the boot root directly when a boot-manager
operation exists.

The app has these five named routes:

- **Overview** is the safe entry point. It reports connection, observed facts,
  boot-root state, and the next available operation without guessing.
- **Deploy** owns the three stages **Provision → Prepare → Action**. Its
  Prepare stage contains the optional Mode 1 graft task; it is not a separate
  route.
- **Entries** lists managed entries and discovered BLS evidence. Managed rows
  expose their read-only mode badge, default, remove, and Deploy preparation
  actions; discovered rows have no mutation controls.
- **Settings** edits the existing `canoe.cfg` policy keys (`menu-mode`,
  `key-window`, and `menu-timeout`).
- **Diagnostics** exposes protocol activity, evidence, paths, digests, and
  export recovery details that are not needed for ordinary operations.

## How Overview decides

The decision is deliberately different on each host:

- **Linux or Windows desktop:** enter Super Fastboot when the device is ready.
  The desktop sidecar waits for a `fastboot.identify` answer. A BDS answer
  keeps the operator on Overview with measured facts; an answer without BDS
  leaves Overview with the fresh-install lane available. A missing fastboot
  tool or failed request remains a visible failure instead of being treated as
  an installed device.
- **KernelSU Android:** the app asks the sidecar for `config.show` against the
  local boot root directly. It never waits for fastboot. A readable root keeps
  Overview available with its derived lanes; a missing root exposes the
  fresh-install lane, while another read failure remains visible and offers
  the repair lane.

The status band is shared by all routes. It reports connection/transport,
slot, BDS version, and expandable details such as boot-root and staged-set
information. Values that have not been answered are shown as **Unknown**;
the app never infers a slot or a BDS version from context.

## Safety and intended use

This project must **not** be used for cheating. Hiding bootloader state is
useless: the TEE still exposes the real device identity and can cause device
blacklisting. Device TEE keys cannot be replaced like TrickyStore.

Root itself does not reduce device security, but unverified modules and modified
ROMs absolutely can. Netflix playback can work, but video processing remains in
the TEE and cannot be dumped. Gaming is not a ban-avoidance mechanism: do not
cheat, and remember that repeated reports can still blacklist a device.
